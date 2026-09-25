#include "games/go.h"

#include "games/registry.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chess {

namespace {

// 4 邻域
constexpr int kOrtho[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
// 4 个对角（眼位判定用）
constexpr int kDiag[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

} // namespace

// ---------------------------------------------------------------------------
//  描述符
// ---------------------------------------------------------------------------

GameDesc GoGame::desc() const
{
    static const std::vector<BoardPreset> boards = {
        {"9 x 9", 9, 9},
        {"13 x 13", 13, 13},
        {"19 x 19 标准", 19, 19},
    };

    static const std::vector<LevelDesc> levels = {
        {"入门", "标出所有合法着点，禁着点用灰叉"},
        {"初级", "额外提示打吃（自己该跑 / 对方能吃）"},
        {"中级", "推荐着点并给出粗略目数差"},
        {"高级", "给出蒙特卡洛胜率与主要变化"},
        {"大师", "胜率 + 主变 + 打劫提示，搜索预算最大"},
    };

    GameDesc d;
    d.id             = "go";
    d.name           = "围棋";
    d.blurb          = "中国规则数子法：提子、禁自杀、简单劫、停一手终局；贴目按棋盘大小";
    d.variants       = {};
    d.boardPresets   = boards;
    d.levels         = levels;
    d.handicapName   = "让子";
    d.create         = []() -> std::unique_ptr<IGame> { return std::make_unique<GoGame>(); };
    d.createAi       = nullptr;   // 在注册表里填
    return d;
}

std::string GoGame::variantName() const
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d 路  贴目 %.1f", cols_, komi_);
    return buf;
}

// ---------------------------------------------------------------------------
//  生命周期
// ---------------------------------------------------------------------------

void GoGame::setup(const GameConfig& cfg)
{
    cfg_ = cfg;

    // 只接受正方形棋盘
    int n = std::clamp(cfg.cols, 5, 25);

    // 贴目按棋盘大小给一个常见的默认值：
    //   9 路常用 5.5 / 13 路常用 6.5 / 19 路常用 7.5（中国规则）
    cols_ = rows_ = n;
    if (n <= 9)
    {
        komi_ = 5.5;
    }
    else if (n <= 13)
    {
        komi_ = 6.5;
    }
    else
    {
        komi_ = 7.5;
    }

    clearBoard();

    sideToMove_          = Side::First;
    status_              = GameStatus::Playing;
    koPoint_             = Coord{};
    consecutivePasses_   = 0;
    history_.clear();

    // 让子：黑先摆 N 子，然后白先走（这是通行做法）
    const int handicap = std::clamp(cfg.handicap, 0, 9);
    if (handicap >= 2)
    {
        placeHandicap(handicap);
        sideToMove_ = Side::Second;
    }
}

void GoGame::clearBoard()
{
    cells_.assign(static_cast<size_t>(cols_) * rows_, 0);
}

void GoGame::placeHandicap(int count)
{
    // 星位坐标（按棋盘大小取常见位置）
    const int edge = (cols_ >= 13) ? 3 : 2;
    const int lo   = edge;
    const int hi   = cols_ - 1 - edge;
    const int mid  = cols_ / 2;

    const Coord pts[9] = {
        {lo, lo}, {hi, hi}, {hi, lo}, {lo, hi},   // 4 个角
        {lo, mid}, {hi, mid}, {mid, lo}, {mid, hi},   // 4 个边星
        {mid, mid},                                   // 天元
    };

    // 让子摆放顺序是约定的：先角、再边星、最后天元
    for (int i = 0; i < count && i < 9; ++i)
    {
        cells_[static_cast<size_t>(pts[i].y) * cols_ + pts[i].x] = 1;   // 黑
    }
}

BoardSpec GoGame::boardSpec() const
{
    BoardSpec s;
    s.kind = BoardKind::Intersections;
    s.cols = cols_;
    s.rows = rows_;
    return s;
}

// ---------------------------------------------------------------------------
//  气 / 提子
// ---------------------------------------------------------------------------

int GoGame::countLiberties(int x, int y, uint8_t stone) const
{
    if (at(x, y) != stone)
    {
        return -1;
    }

    // 用一张 visited 位图做洪泛
    std::vector<uint8_t> seen(static_cast<size_t>(cols_) * rows_, 0);
    std::vector<Coord>   stack;
    stack.push_back(Coord{x, y});
    seen[static_cast<size_t>(y) * cols_ + x] = 1;

    int liberties = 0;
    std::vector<uint8_t> libertySeen(static_cast<size_t>(cols_) * rows_, 0);

    while (!stack.empty())
    {
        const Coord c = stack.back();
        stack.pop_back();

        for (const auto& d : kOrtho)
        {
            const int nx = c.x + d[0];
            const int ny = c.y + d[1];
            const uint8_t v = at(nx, ny);

            if (v == 0)
            {
                const size_t idx = static_cast<size_t>(ny) * cols_ + nx;
                if (!libertySeen[idx])
                {
                    libertySeen[idx] = 1;
                    ++liberties;
                }
                continue;
            }
            if (v != stone)
            {
                continue;
            }
            const size_t idx = static_cast<size_t>(ny) * cols_ + nx;
            if (!seen[idx])
            {
                seen[idx] = 1;
                stack.push_back(Coord{nx, ny});
            }
        }
    }
    return liberties;
}

int GoGame::libertiesAt(Coord c) const
{
    const uint8_t v = at(c.x, c.y);
    if (v == 0 || v == kWall)
    {
        return -1;
    }
    return countLiberties(c.x, c.y, v);
}

int GoGame::removeDeadGroup(int x, int y, uint8_t stone, std::vector<Coord>* outRemoved)
{
    // 洪泛出整块，然后全部清空；调用方需先确认它已无气
    std::vector<uint8_t> seen(static_cast<size_t>(cols_) * rows_, 0);
    std::vector<Coord>   stack;
    stack.push_back(Coord{x, y});
    seen[static_cast<size_t>(y) * cols_ + x] = 1;

    int removed = 0;
    while (!stack.empty())
    {
        const Coord c = stack.back();
        stack.pop_back();
        cells_[static_cast<size_t>(c.y) * cols_ + c.x] = 0;
        ++removed;

        if (outRemoved)
        {
            outRemoved->push_back(c);
        }

        for (const auto& d : kOrtho)
        {
            const int nx = c.x + d[0];
            const int ny = c.y + d[1];
            if (nx < 0 || ny < 0 || nx >= cols_ || ny >= rows_)
            {
                continue;
            }
            const size_t idx = static_cast<size_t>(ny) * cols_ + nx;
            if (cells_[idx] == stone && !seen[idx])
            {
                seen[idx] = 1;
                stack.push_back(Coord{nx, ny});
            }
        }
    }
    return removed;
}

bool GoGame::isEye(Coord c, uint8_t stone) const
{
    if (at(c.x, c.y) != 0)
    {
        return false;
    }

    // 四邻必须全是本方棋子
    for (const auto& d : kOrtho)
    {
        const int nx = c.x + d[0];
        const int ny = c.y + d[1];
        // 棋盘外算"本方"——边角的眼只靠 3 个邻居也能成立
        if (nx < 0 || ny < 0 || nx >= cols_ || ny >= rows_)
        {
            continue;
        }
        if (cells_[static_cast<size_t>(ny) * cols_ + nx] != stone)
        {
            return false;
        }
    }

    // 对角：盘中有 >= 2 个本方子、边角有 >= 1 个，才算真眼
    const bool onEdge = (c.x == 0 || c.y == 0 || c.x == cols_ - 1 || c.y == rows_ - 1);
    int        own    = 0;
    int        opp    = 0;
    for (const auto& d : kDiag)
    {
        const int nx = c.x + d[0];
        const int ny = c.y + d[1];
        if (nx < 0 || ny < 0 || nx >= cols_ || ny >= rows_)
        {
            continue;
        }
        const uint8_t v = cells_[static_cast<size_t>(ny) * cols_ + nx];
        if (v == stone)
        {
            ++own;
        }
        else if (v != 0)
        {
            ++opp;
        }
    }

    const int need = onEdge ? 1 : 2;
    return own >= need && opp == 0;
}

bool GoGame::wouldBeSuicide(Coord c, uint8_t stone) const
{
    if (at(c.x, c.y) != 0)
    {
        return true;
    }

    // 在副本上落子，先提对方，再看自己是否有气
    GoGame tmp = *this;
    tmp.cells_[static_cast<size_t>(c.y) * cols_ + c.x] = stone;

    const uint8_t opp = (stone == 1) ? 2 : 1;
    for (const auto& d : kOrtho)
    {
        const int nx = c.x + d[0];
        const int ny = c.y + d[1];
        if (tmp.at(nx, ny) == opp && tmp.countLiberties(nx, ny, opp) == 0)
        {
            tmp.removeDeadGroup(nx, ny, opp);
        }
    }

    return tmp.countLiberties(c.x, c.y, stone) == 0;
}

// ---------------------------------------------------------------------------
//  规则
// ---------------------------------------------------------------------------

bool GoGame::hasPieceOf(Coord c, Side s) const
{
    const uint8_t v = at(c.x, c.y);
    if (v == 0 || v == kWall)
    {
        return false;
    }
    return v == ((s == Side::First) ? 1 : 2);
}

bool GoGame::isLegal(const Move& m) const
{
    if (status_ != GameStatus::Playing)
    {
        return false;
    }

    // pass
    if (m.isPass())
    {
        return true;
    }

    const Coord c = m.to;
    if (c.x < 0 || c.y < 0 || c.x >= cols_ || c.y >= rows_)
    {
        return false;
    }
    if (cells_[static_cast<size_t>(c.y) * cols_ + c.x] != 0)
    {
        return false;
    }

    // 简单劫
    if (koPoint_.valid() && c == koPoint_)
    {
        return false;
    }

    const uint8_t stone = (sideToMove_ == Side::First) ? 1 : 2;
    return !wouldBeSuicide(c, stone);
}

std::vector<Move> GoGame::legalMoves() const
{
    std::vector<Move> out;
    if (status_ != GameStatus::Playing)
    {
        return out;
    }

    out.reserve(static_cast<size_t>(cols_) * rows_ + 1);
    for (int y = 0; y < rows_; ++y)
    {
        for (int x = 0; x < cols_; ++x)
        {
            Move m{};
            m.to = Coord{x, y};
            if (isLegal(m))
            {
                out.push_back(m);
            }
        }
    }

    // pass 总是合法的
    Move pass{};
    out.push_back(pass);
    return out;
}

bool GoGame::apply(const Move& m)
{
    if (!isLegal(m))
    {
        return false;
    }

    MoveRec rec;
    rec.at       = m.to;
    rec.pass     = m.isPass();
    rec.koBefore = koPoint_;

    if (rec.pass)
    {
        ++consecutivePasses_;
        history_.push_back(rec);

        if (consecutivePasses_ >= 2)
        {
            finishWithScoring();
        }
        else
        {
            sideToMove_ = otherSide(sideToMove_);
        }
        return true;
    }

    const uint8_t stone = (sideToMove_ == Side::First) ? 1 : 2;
    const uint8_t opp   = (stone == 1) ? 2 : 1;

    cells_[static_cast<size_t>(m.to.y) * cols_ + m.to.x] = stone;

    // 提掉对方无气的棋块；记录每一枚被提的子（悔棋要按位复原）
    int capturedTotal  = 0;
    int capturedGroups = 0;
    Coord lastCapturedSeat{};
    int   lastCapturedCount = 0;

    for (const auto& d : kOrtho)
    {
        const int nx = m.to.x + d[0];
        const int ny = m.to.y + d[1];
        if (at(nx, ny) != opp)
        {
            continue;
        }
        // 注意：必须每个方向重新判气；同一块可能被多个方向重复遍历，
        // 但第一次提掉后 at() 就变成 0，不会重复计入。
        if (countLiberties(nx, ny, opp) == 0)
        {
            std::vector<Coord> removed;
            const int          n = removeDeadGroup(nx, ny, opp, &removed);
            capturedTotal += n;
            ++capturedGroups;
            lastCapturedSeat  = Coord{nx, ny};
            lastCapturedCount = n;

            for (const Coord& rc : removed)
            {
                rec.removedPos.push_back(rc);
                rec.removedColor.push_back(opp);
            }
        }
    }
    rec.captured = capturedTotal;

    // 简单劫点判定：只提掉"一块、一子"，且落下的这一子提完后自己也只剩一口气
    koPoint_ = Coord{};
    if (capturedGroups == 1 && lastCapturedCount == 1)
    {
        if (countLiberties(m.to.x, m.to.y, stone) == 1)
        {
            koPoint_ = lastCapturedSeat;
        }
    }

    consecutivePasses_ = 0;
    history_.push_back(rec);

    // 棋盘满了也终局（极少发生，但避免出现"无处可下又没人停手"的死局）
    bool anyEmpty = false;
    for (uint8_t v : cells_)
    {
        if (v == 0)
        {
            anyEmpty = true;
            break;
        }
    }

    if (!anyEmpty)
    {
        finishWithScoring();
        return true;
    }

    sideToMove_ = otherSide(sideToMove_);
    return true;
}

void GoGame::finishWithScoring()
{
    const Score s = score();
    if (s.black > s.white)
    {
        status_ = GameStatus::FirstWin;
    }
    else if (s.white > s.black)
    {
        status_ = GameStatus::SecondWin;
    }
    else
    {
        status_ = GameStatus::Draw;
    }
}

bool GoGame::undo()
{
    if (history_.empty())
    {
        return false;
    }

    const MoveRec rec = history_.back();
    history_.pop_back();

    status_            = GameStatus::Playing;
    consecutivePasses_ = 0;
    koPoint_           = rec.koBefore;

    if (rec.pass)
    {
        sideToMove_ = otherSide(sideToMove_);
        return true;
    }

    // 移除自己刚下的子
    cells_[static_cast<size_t>(rec.at.y) * cols_ + rec.at.x] = 0;

    // 把被提掉的每一枚子放回原位
    for (size_t i = 0; i < rec.removedPos.size(); ++i)
    {
        const Coord   p = rec.removedPos[i];
        const uint8_t v = rec.removedColor[i];
        cells_[static_cast<size_t>(p.y) * cols_ + p.x] = v;
    }

    sideToMove_ = otherSide(sideToMove_);
    return true;
}

// ---------------------------------------------------------------------------
//  计分（中国规则数子法）
// ---------------------------------------------------------------------------

GoGame::Score GoGame::score() const
{
    Score s;
    s.komi = komi_;

    for (uint8_t v : cells_)
    {
        if (v == 1)
        {
            ++s.black;
        }
        else if (v == 2)
        {
            ++s.white;
        }
    }

    // 空地洪泛：只与一色相邻的空区归该色
    std::vector<uint8_t> seen(static_cast<size_t>(cols_) * rows_, 0);
    for (int y = 0; y < rows_; ++y)
    {
        for (int x = 0; x < cols_; ++x)
        {
            const size_t start = static_cast<size_t>(y) * cols_ + x;
            if (cells_[start] != 0 || seen[start])
            {
                continue;
            }

            std::vector<Coord> region;
            std::vector<Coord> stack;
            stack.push_back(Coord{x, y});
            seen[start] = 1;

            bool touchesBlack = false;
            bool touchesWhite = false;

            while (!stack.empty())
            {
                const Coord c = stack.back();
                stack.pop_back();
                region.push_back(c);

                for (const auto& d : kOrtho)
                {
                    const int nx = c.x + d[0];
                    const int ny = c.y + d[1];
                    if (nx < 0 || ny < 0 || nx >= cols_ || ny >= rows_)
                    {
                        continue;
                    }
                    const size_t idx = static_cast<size_t>(ny) * cols_ + nx;
                    const uint8_t v  = cells_[idx];
                    if (v == 1)
                    {
                        touchesBlack = true;
                    }
                    else if (v == 2)
                    {
                        touchesWhite = true;
                    }
                    else if (!seen[idx])
                    {
                        seen[idx] = 1;
                        stack.push_back(Coord{nx, ny});
                    }
                }
            }

            if (touchesBlack && !touchesWhite)
            {
                s.black += static_cast<int>(region.size());
            }
            else if (touchesWhite && !touchesBlack)
            {
                s.white += static_cast<int>(region.size());
            }
            // 双方都接触 -> 双活/公气，都不算
        }
    }

    return s;
}

// ---------------------------------------------------------------------------
//  指纹 / 文本
// ---------------------------------------------------------------------------

uint64_t GoGame::stateKey() const
{
    uint64_t h = 0xCBF29CE484222325ull;
    h ^= static_cast<uint64_t>(cols_);
    h *= 0x100000001B3ull;
    h ^= static_cast<uint64_t>(sideToMove_ == Side::First ? 1 : 2);
    h *= 0x100000001B3ull;
    for (uint8_t v : cells_)
    {
        h ^= static_cast<uint64_t>(v + 1);
        h *= 0x100000001B3ull;
    }
    return h;
}

std::unique_ptr<IGame> GoGame::clone() const
{
    return std::make_unique<GoGame>(*this);
}

const char* GoGame::sideName(Side s) const
{
    return s == Side::First ? "黑棋" : (s == Side::Second ? "白棋" : "—");
}

std::string GoGame::statusText() const
{
    const Score s = score();

    switch (status_)
    {
    case GameStatus::FirstWin:
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "终局：黑胜（黑 %.1f  白 %.1f）",
                      static_cast<double>(s.black), static_cast<double>(s.white) + s.komi);
        return buf;
    }
    case GameStatus::SecondWin:
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "终局：白胜（黑 %.1f  白 %.1f）",
                      static_cast<double>(s.black), static_cast<double>(s.white) + s.komi);
        return buf;
    }
    case GameStatus::Draw:
        return "终局：和棋";
    default:
        break;
    }

    if (consecutivePasses_ == 1)
    {
        return sideToMove_ == Side::First ? "黑棋已停一手；黑再停手即终局"
                                          : "白棋已停一手；白再停手即终局";
    }
    if (koPoint_.valid())
    {
        return sideToMove_ == Side::First ? "轮到黑棋（注意打劫禁着点）" : "轮到白棋（注意打劫禁着点）";
    }
    return sideToMove_ == Side::First ? "轮到黑棋" : "轮到白棋";
}

std::string GoGame::scoreText() const
{
    const Score s = score();
    char        buf[160];
    std::snprintf(buf, sizeof(buf), "数子：黑 %.1f   白 %.1f（含贴目 %.1f）",
                  static_cast<double>(s.black), static_cast<double>(s.white) + s.komi, s.komi);
    return buf;
}

Move GoGame::lastMove() const
{
    Move m{};
    if (!history_.empty())
    {
        m.to = history_.back().pass ? Coord{} : history_.back().at;
    }
    return m;
}

// ---------------------------------------------------------------------------
//  提示（L1 / L2 由游戏给出）
// ---------------------------------------------------------------------------

HintData GoGame::basicHint(int level) const
{
    HintData h;
    if (status_ != GameStatus::Playing || level <= 0 || level >= 3)
    {
        return h;
    }

    const uint8_t me  = (sideToMove_ == Side::First) ? 1 : 2;
    const uint8_t opp = (me == 1) ? 2 : 1;

    for (int y = 0; y < rows_; ++y)
    {
        for (int x = 0; x < cols_; ++x)
        {
            const Coord c{x, y};
            if (at(x, y) != 0)
            {
                continue;
            }

            const Move m = [&] {
                Move mm{};
                mm.to = c;
                return mm;
            }();

            if (!isLegal(m))
            {
                // 非法：自杀点或打劫点
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Illegal;
                hc.weight = 1.0f;
                h.cells.push_back(hc);
                continue;
            }

            if (level == 1)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Legal;
                hc.weight = 0.3f;
                h.cells.push_back(hc);
                continue;
            }

            // ---- L2：打吃相关的预警 ----
            GoGame tmp = *this;
            tmp.cells_[static_cast<size_t>(y) * cols_ + x] = me;

            // 提掉对方无气块
            int captured = 0;
            for (const auto& d : kOrtho)
            {
                const int nx = x + d[0];
                const int ny = y + d[1];
                if (tmp.at(nx, ny) == opp && tmp.countLiberties(nx, ny, opp) == 0)
                {
                    captured += tmp.removeDeadGroup(nx, ny, opp);
                }
            }

            if (captured > 0)
            {
                // 能提子 —— 最有价值的一手
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Good;
                hc.weight = 1.0f;
                h.cells.push_back(hc);
                continue;
            }

            const int myLibs = tmp.countLiberties(x, y, me);
            if (myLibs == 1)
            {
                // 自己走这里会变成"被打吃" —— 通常是坏棋
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Danger;
                hc.weight = 0.8f;
                h.cells.push_back(hc);
                continue;
            }

            HintCell hc;
            hc.coord  = c;
            hc.kind   = HintKind::Legal;
            hc.weight = 0.3f;
            h.cells.push_back(hc);
        }
    }

    if (level >= 2)
    {
        // 自己处于被打吃的棋块：标红（需要应手）
        for (int y = 0; y < rows_; ++y)
        {
            for (int x = 0; x < cols_; ++x)
            {
                if (at(x, y) != me)
                {
                    continue;
                }
                if (countLiberties(x, y, me) == 1)
                {
                    HintCell hc;
                    hc.coord  = Coord{x, y};
                    hc.kind   = HintKind::Threat;
                    hc.weight = 0.9f;
                    h.cells.push_back(hc);
                }
            }
        }
    }

    h.notes.push_back(level == 1 ? "白点=合法着点，灰叉=非法（自杀或打劫禁着）"
                                 : "绿=可提子，橙=自陷打吃，红=我方被打吃的子");

    if (koPoint_.valid())
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "打劫禁着点：(%d, %d) —— 本手不能立即提回",
                      koPoint_.x, koPoint_.y);
        h.notes.push_back(buf);
    }

    return h;
}

// ---------------------------------------------------------------------------
//  绘制
// ---------------------------------------------------------------------------

void GoGame::drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
{
    (void)ctx;

    // 星位：9 路 5 个 + 天元；13/19 路用 4 角星 + 天元（19 路再补四个边星）
    const int edge = (cols_ >= 13) ? 3 : 2;
    const int lo   = edge;
    const int hi   = cols_ - 1 - edge;
    const int mid  = cols_ / 2;

    std::vector<Coord> stars = {
        {lo, lo}, {hi, hi}, {hi, lo}, {lo, hi}, {mid, mid},
    };
    if (cols_ >= 19)
    {
        stars.push_back(Coord{lo, mid});
        stars.push_back(Coord{hi, mid});
        stars.push_back(Coord{mid, lo});
        stars.push_back(Coord{mid, hi});
    }

    const int r = std::max(2, bv.cell() / 12);
    setfillcolor(theme::kBoardLine, img);
    for (const Coord& s : stars)
    {
        if (bv.inBounds(s))
        {
            fillellipse(bv.px(s), bv.py(s), r, r, img);
        }
    }
}

void GoGame::drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const
{
    const uint8_t v = at(c.x, c.y);
    if (v == 0 || v == kWall)
    {
        return;
    }

    // 围棋棋子比格子略小一点，视觉上更接近真实比例
    const int sz = bv.pieceSize(0.90);

    const char* sprite = (v == 1) ? "black" : "white";
    if (ctx.atlas && ctx.atlas->has(sprite))
    {
        // dest 必须显式传 img：离屏渲染时省略它会把精灵画到窗口上
        ctx.atlas->draw(sprite, bv.spriteX(c, sz), bv.spriteY(c, sz), sz, img);
    }
    else
    {
        // 兜底：图集缺图时才自绘（带一点高光，接近真实棋子观感）
        const int cx = bv.px(c);
        const int cy = bv.py(c);
        const int r  = sz / 2;

        setfillcolor((v == 1) ? EGERGB(0x2A, 0x2E, 0x34) : EGERGB(0xF2, 0xF4, 0xF8), img);
        fillellipse(cx, cy, r, r, img);
        setcolor(theme::kBoardLine, img);
        circle(cx, cy, r, img);

        setfillcolor((v == 1) ? EGERGB(0x5A, 0x60, 0x6A) : EGERGB(0xFF, 0xFF, 0xFF), img);
        fillellipse(cx - r / 3, cy - r / 3, r / 3, r / 3, img);
    }
}

void GoGame::drawOverlay(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
{
    // 打劫禁着点：无论等级都标出来 —— 这是一条"不看就会走出违规手"的信息
    if (ctx.showHint && koPoint_.valid() && bv.inBounds(koPoint_))
    {
        setcolor(theme::kDanger, img);
        setlinewidth(2.0f, img);
        const int d = std::max(4, bv.cell() / 6);
        const int cx = bv.px(koPoint_);
        const int cy = bv.py(koPoint_);
        line(cx - d, cy - d, cx + d, cy + d, img);
        line(cx - d, cy + d, cx + d, cy - d, img);
        setlinewidth(1.0f, img);
    }
}

} // namespace chess
