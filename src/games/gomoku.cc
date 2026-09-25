#include "games/gomoku.h"

#include "games/registry.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>

namespace chess {

// ---------------------------------------------------------------------------
//  描述符
// ---------------------------------------------------------------------------

GameDesc GomokuGame::desc() const
{
    static const std::vector<std::string> variants = {"自由规则", "黑棋禁手"};

    static const std::vector<BoardPreset> boards = {
        {"9 x 9", 9, 9},
        {"13 x 13", 13, 13},
        {"15 x 15 标准", 15, 15},
        {"19 x 19", 19, 19},
    };

    static const std::vector<LevelDesc> levels = {
        {"入门", "标出所有可落点，禁手点用灰叉"},
        {"初级", "额外标出对手的成五 / 活四威胁点"},
        {"中级", "推荐最佳着法并给出局面评估分"},
        {"高级", "给出后续主要变化与搜索深度"},
        {"大师", "给出胜率、连珠威胁清单与更深搜索"},
    };

    GameDesc d;
    d.id           = "gomoku";
    d.name         = "五子棋";
    d.blurb        = "先连成五子者胜；可选黑棋禁手（长连 / 双四 / 双活三）";
    d.variants     = variants;
    d.boardPresets = boards;
    d.levels       = levels;
    d.create       = []() -> std::unique_ptr<IGame> { return std::make_unique<GomokuGame>(); };
    d.createAi     = nullptr;   // 在注册表里填，避免 games/ 反向依赖 ai/
    return d;
}

std::string GomokuGame::variantName() const
{
    return ruleForbidden() ? "黑棋禁手" : "自由规则";
}

// ---------------------------------------------------------------------------
//  生命周期
// ---------------------------------------------------------------------------

void GomokuGame::setup(const GameConfig& cfg)
{
    cfg_ = cfg;

    int n = std::clamp(cfg.cols, 5, 25);
    if (cfg.rows != cfg.cols)
    {
        n = std::clamp(cfg.rows, 5, 25);
    }
    board_.reset(n, n);

    sideToMove_ = Side::First;
    status_     = GameStatus::Playing;
    history_.clear();
}

BoardSpec GomokuGame::boardSpec() const
{
    BoardSpec s;
    s.kind = BoardKind::Intersections;   // 落子在交叉点
    s.cols = board_.cols;
    s.rows = board_.rows;
    return s;
}

// ---------------------------------------------------------------------------
//  规则
// ---------------------------------------------------------------------------

bool GomokuGame::hasPieceOf(Coord c, Side s) const
{
    return board_.inBounds(c.x, c.y) && board_.at(c.x, c.y) == sideValue(s);
}

bool GomokuGame::moveIsForbidden(Coord c) const
{
    if (!ruleForbidden() || sideToMove_ != Side::First)
    {
        return false;
    }
    // 禁手判定要在"假设已落子"的局面下做，这里临时落一枚再撤回
    gomoku::Board tmp = board_;
    tmp.set(c.x, c.y, 1);
    return gomoku::checkForbidden(tmp, c.x, c.y).forbidden;
}

std::vector<Move> GomokuGame::legalMoves() const
{
    std::vector<Move> out;
    if (status_ != GameStatus::Playing)
    {
        return out;
    }

    out.reserve(static_cast<size_t>(board_.size()));
    for (int y = 0; y < board_.rows; ++y)
    {
        for (int x = 0; x < board_.cols; ++x)
        {
            if (!board_.empty(x, y))
            {
                continue;
            }
            const Coord c{x, y};
            if (moveIsForbidden(c))
            {
                continue;
            }
            Move m{};
            m.to = c;
            out.push_back(m);
        }
    }
    return out;
}

bool GomokuGame::isLegal(const Move& m) const
{
    if (status_ != GameStatus::Playing || !board_.inBounds(m.to.x, m.to.y))
    {
        return false;
    }
    if (!board_.empty(m.to.x, m.to.y))
    {
        return false;
    }
    return !moveIsForbidden(m.to);
}

bool GomokuGame::apply(const Move& m)
{
    if (!isLegal(m))
    {
        return false;
    }

    const uint8_t me = sideValue(sideToMove_);
    board_.set(m.to.x, m.to.y, me);
    history_.push_back(m.to);

    updateStatusAfterMove(m.to);
    if (status_ == GameStatus::Playing)
    {
        sideToMove_ = otherSide(sideToMove_);
    }
    return true;
}

void GomokuGame::updateStatusAfterMove(Coord c)
{
    const uint8_t me = board_.at(c.x, c.y);

    if (gomoku::isWinAt(board_, c.x, c.y, me))
    {
        status_ = (me == 1) ? GameStatus::FirstWin : GameStatus::SecondWin;
        return;
    }

    // 黑棋禁手：落子后自己构成禁手 -> 判负
    if (ruleForbidden() && me == 1 && gomoku::checkForbidden(board_, c.x, c.y).forbidden)
    {
        status_ = GameStatus::SecondWin;
        return;
    }

    // 棋盘满了 -> 和棋
    bool full = true;
    for (uint8_t v : board_.cells)
    {
        if (v == 0)
        {
            full = false;
            break;
        }
    }
    if (full)
    {
        status_ = GameStatus::Draw;
    }
}

bool GomokuGame::undo()
{
    if (history_.empty())
    {
        return false;
    }

    const Coord c = history_.back();
    history_.pop_back();
    board_.set(c.x, c.y, 0);

    status_ = GameStatus::Playing;

    // 手数为偶数 -> 轮到先手；奇数 -> 轮到后手
    sideToMove_ = (history_.size() % 2 == 0) ? Side::First : Side::Second;
    return true;
}

// ---------------------------------------------------------------------------
//  指纹 / 克隆
// ---------------------------------------------------------------------------

uint64_t GomokuGame::stateKey() const
{
    // Zobrist 风格的增量哈希用不上（局面不大），直接对整盘做 FNV-1a。
    // 五子棋的重复局面概率极低，这里主要给"AI 分析结果是否仍然有效"做键。
    uint64_t h = 0xCBF29CE484222325ull;
    h ^= static_cast<uint64_t>(board_.cols);
    h *= 0x100000001B3ull;
    h ^= static_cast<uint64_t>(board_.rows);
    h *= 0x100000001B3ull;
    h ^= static_cast<uint64_t>(sideToMove_ == Side::First ? 1 : 2);
    h *= 0x100000001B3ull;

    for (uint8_t v : board_.cells)
    {
        h ^= static_cast<uint64_t>(v + 1);
        h *= 0x100000001B3ull;
    }
    return h;
}

std::unique_ptr<IGame> GomokuGame::clone() const
{
    return std::make_unique<GomokuGame>(*this);
}

// ---------------------------------------------------------------------------
//  文本
// ---------------------------------------------------------------------------

const char* GomokuGame::sideName(Side s) const
{
    return s == Side::First ? "黑棋" : (s == Side::Second ? "白棋" : "—");
}

std::string GomokuGame::statusText() const
{
    switch (status_)
    {
    case GameStatus::FirstWin:
        return ruleForbidden() ? "黑棋成五，胜！（若为禁手判负则不在此列）" : "黑棋成五，胜！";
    case GameStatus::SecondWin:
        return ruleForbidden() ? "白棋胜！（或黑棋触犯禁手判负）" : "白棋成五，胜！";
    case GameStatus::Draw:
        return "棋盘已满，和棋";
    default:
        break;
    }

    if (ruleForbidden() && sideToMove_ == Side::First)
    {
        return "轮到黑棋（注意：长连 / 双四 / 双活三为禁手，判负）";
    }
    return sideToMove_ == Side::First ? "轮到黑棋" : "轮到白棋";
}

std::string GomokuGame::scoreText() const
{
    int black = 0;
    int white = 0;
    for (uint8_t v : board_.cells)
    {
        if (v == 1) { ++black; }
        else if (v == 2) { ++white; }
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "手数 %d   黑 %d  白 %d", static_cast<int>(history_.size()),
                  black, white);
    return buf;
}

Move GomokuGame::lastMove() const
{
    Move m{};
    if (!history_.empty())
    {
        m.to = history_.back();
    }
    return m;
}

// ---------------------------------------------------------------------------
//  候选点
// ---------------------------------------------------------------------------

std::vector<Coord> GomokuGame::candidateMoves() const
{
    std::vector<Coord> out;

    // 空盘：直接给天元
    bool anyStone = false;
    for (uint8_t v : board_.cells)
    {
        if (v != 0)
        {
            anyStone = true;
            break;
        }
    }
    if (!anyStone)
    {
        out.push_back(Coord{board_.cols / 2, board_.rows / 2});
        return out;
    }

    // 只考虑"已有棋子 2 格邻域"内的空点：五子棋的落子几乎不可能离现有棋子太远，
    // 这个剪枝把候选从 225 降到几十，是搜索能跑起来的关键。
    constexpr int kRadius = 2;
    for (int y = 0; y < board_.rows; ++y)
    {
        for (int x = 0; x < board_.cols; ++x)
        {
            if (!board_.empty(x, y))
            {
                continue;
            }

            // 只看有棋子在附近的位置
            // 注意：变量名不能叫 near —— windows.h 里 near/far 是遗留宏
            bool hasNeighbor = false;
            for (int dy = -kRadius; dy <= kRadius && !hasNeighbor; ++dy)
            {
                for (int dx = -kRadius; dx <= kRadius; ++dx)
                {
                    if (dx == 0 && dy == 0)
                    {
                        continue;
                    }
                    if (board_.at(x + dx, y + dy) != 0 &&
                        board_.at(x + dx, y + dy) != gomoku::Board::kWall)
                    {
                        hasNeighbor = true;
                        break;
                    }
                }
            }
            if (!hasNeighbor)
            {
                continue;
            }

            const Coord c{x, y};
            if (moveIsForbidden(c))
            {
                continue;
            }
            out.push_back(c);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
//  提示（L1 / L2 由游戏给出，L3+ 由搜索给出）
// ---------------------------------------------------------------------------

HintData GomokuGame::basicHint(int level) const
{
    HintData h;
    if (status_ != GameStatus::Playing || level <= 0 || level >= 3)
    {
        // L3 起交给搜索，避免把整盘的低级标记盖在推荐着法上
        return h;
    }

    const uint8_t me  = sideValue(sideToMove_);
    const uint8_t opp = (me == 1) ? 2 : 1;

    for (int y = 0; y < board_.rows; ++y)
    {
        for (int x = 0; x < board_.cols; ++x)
        {
            if (!board_.empty(x, y))
            {
                continue;
            }

            const Coord c{x, y};

            // ---- L1：可落点 / 禁手点 ----
            if (level == 1)
            {
                HintCell hc;
                hc.coord = c;
                if (moveIsForbidden(c))
                {
                    hc.kind   = HintKind::Illegal;
                    hc.weight = 1.0f;
                }
                else
                {
                    hc.kind   = HintKind::Legal;
                    hc.weight = 0.35f;
                }
                h.cells.push_back(hc);
                continue;
            }

            // ---- L2：威胁预警 ----
            if (moveIsForbidden(c))
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Illegal;
                hc.weight = 1.0f;
                h.cells.push_back(hc);
                continue;
            }

            gomoku::Board tmp = board_;
            tmp.set(x, y, opp);
            const gomoku::Shape oppShape = gomoku::bestShape(tmp, x, y, opp);

            if (oppShape == gomoku::Shape::Five || oppShape == gomoku::Shape::Overline)
            {
                // 对手下一步能在这里直接成五 —— 最紧急
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Threat;
                hc.weight = 1.0f;
                h.cells.push_back(hc);
                continue;
            }
            if (oppShape == gomoku::Shape::OpenFour)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Threat;
                hc.weight = 0.85f;
                h.cells.push_back(hc);
                continue;
            }
            if (oppShape == gomoku::Shape::Four)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Danger;
                hc.weight = 0.7f;
                h.cells.push_back(hc);
                continue;
            }
            if (oppShape == gomoku::Shape::OpenThree)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Danger;
                hc.weight = 0.45f;
                h.cells.push_back(hc);
                continue;
            }

            const gomoku::Shape myShape = gomoku::bestShape(board_, x, y, me);
            if (myShape == gomoku::Shape::OpenFour || myShape == gomoku::Shape::OpenThree)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Good;
                hc.weight = (myShape == gomoku::Shape::OpenFour) ? 0.9f : 0.5f;
                h.cells.push_back(hc);
                continue;
            }
            if (myShape == gomoku::Shape::Four)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Good;
                hc.weight = 0.6f;
                h.cells.push_back(hc);
            }
        }
    }

    h.notes.push_back(level == 1 ? "白圈=可落点，灰叉=禁手点（黑棋）"
                                 : "红=对手可成五/活四，橙=对手冲四/活三，绿=我的好形");
    return h;
}

// ---------------------------------------------------------------------------
//  绘制
// ---------------------------------------------------------------------------

void GomokuGame::drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
{
    (void)ctx;

    const int c = bv.cell();

    // 星位（天元 + 四角），按棋盘大小选位置
    int inset = 3;   // 15x15 标准是 3
    if (board_.cols <= 9)
    {
        inset = 2;
    }
    else if (board_.cols >= 19)
    {
        inset = 3;
    }

    const int midX = board_.cols / 2;
    const int midY = board_.rows / 2;
    const int loX  = inset;
    const int hiX  = board_.cols - 1 - inset;
    const int loY  = inset;
    const int hiY  = board_.rows - 1 - inset;

    const Coord stars[5] = {
        {midX, midY}, {loX, loY}, {hiX, loY}, {loX, hiY}, {hiX, hiY},
    };

    setfillcolor(theme::kBoardLine, img);
    for (const Coord& s : stars)
    {
        if (!bv.inBounds(s))
        {
            continue;
        }
        const int r = std::max(2, c / 14);
        fillellipse(bv.px(s), bv.py(s), r, r, img);
    }
}

void GomokuGame::drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const
{
    if (!board_.inBounds(c.x, c.y))
    {
        return;
    }
    const uint8_t v = board_.at(c.x, c.y);
    if (v == 0)
    {
        return;
    }

    // 棋子略小于格距，留出棋盘线
    const int sz = bv.pieceSize(0.86);

    const char* sprite = (v == 1) ? "black" : "white";
    if (ctx.atlas && ctx.atlas->has(sprite))
    {
        // dest 必须传 img —— 离屏渲染时省略它会把精灵画到窗口上
        ctx.atlas->draw(sprite, bv.spriteX(c, sz), bv.spriteY(c, sz), sz, img);
    }
    else
    {
        // 兜底：图集缺图时才自绘
        setfillcolor((v == 1) ? theme::kSideFirst : theme::kSideSecond, img);
        fillellipse(bv.px(c), bv.py(c), sz / 2, sz / 2, img);
        setcolor(theme::kBoardLine, img);
        circle(bv.px(c), bv.py(c), sz / 2, img);
    }
}

} // namespace chess
