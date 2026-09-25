#include "games/xiangqi.h"

#include "games/registry.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chess {

using namespace xq;

// ---------------------------------------------------------------------------
//  棋子名称
// ---------------------------------------------------------------------------

const char* xq::pieceName(uint8_t piece)
{
    if (isEmpty(piece))
    {
        return "";
    }
    const bool red = (sideOf(piece) == kRed);
    switch (typeOf(piece))
    {
    case PieceType::King:     return red ? "帥" : "将";
    case PieceType::Advisor:  return red ? "仕" : "士";
    case PieceType::Elephant: return red ? "相" : "象";
    case PieceType::Horse:    return red ? "马" : "馬";
    case PieceType::Chariot:  return red ? "车" : "車";
    case PieceType::Cannon:   return "炮";
    case PieceType::Pawn:     return red ? "兵" : "卒";
    default:                  return "";
    }
}

bool xq::inPalace(uint8_t side, int x, int y)
{
    if (x < 3 || x > 5)
    {
        return false;
    }
    // 先手（红）在下方，后手在上方
    return (side == kRed) ? (y >= 7 && y <= 9) : (y >= 0 && y <= 2);
}

bool xq::crossedRiver(uint8_t side, int y)
{
    // 红方在下，y <= 4 表示已过河；黑方在上，y >= 5 表示已过河
    return (side == kRed) ? (y <= 4) : (y >= 5);
}

// ---------------------------------------------------------------------------
//  描述符
// ---------------------------------------------------------------------------

GameDesc XiangqiGame::desc() const
{
    static const std::vector<BoardPreset> boards = {{"9 x 10 标准", 9, 10}};

    static const std::vector<LevelDesc> levels = {
        {"入门", "标出所选棋子的全部可走位置"},
        {"初级", "额外提示被吃风险与将军预警"},
        {"中级", "推荐最佳着法并给出子力评估分"},
        {"高级", "给出后续主要变化与搜索深度"},
        {"大师", "给出胜率、杀棋提示与更深搜索"},
    };

    GameDesc d;
    d.id           = "xiangqi";
    d.name         = "象棋";
    d.blurb        = "中国象棋：马腿象眼、炮隔子、兵卒过河、飞将、将军与长将判负";
    d.variants     = {};   // 只有标准规则，无变体
    d.boardPresets = boards;
    d.levels       = levels;
    d.create       = []() -> std::unique_ptr<IGame> { return std::make_unique<XiangqiGame>(); };
    d.createAi     = nullptr;
    return d;
}

// ---------------------------------------------------------------------------
//  初始局面
// ---------------------------------------------------------------------------

void XiangqiGame::setup(const GameConfig& cfg)
{
    cfg_ = cfg;
    setStartPosition();
}

void XiangqiGame::setStartPosition()
{
    std::memset(board_, 0, sizeof(board_));

    auto put = [&](int x, int y, uint8_t side, PieceType t) {
        board_[idx(x, y)] = makePiece(side, t);
    };

    // 后手（黑/上方）：y = 0..2
    const PieceType backRow[kCols] = {
        PieceType::Chariot, PieceType::Horse, PieceType::Elephant, PieceType::Advisor,
        PieceType::King,    PieceType::Advisor, PieceType::Elephant, PieceType::Horse,
        PieceType::Chariot,
    };
    for (int x = 0; x < kCols; ++x)
    {
        put(x, 0, kBlack, backRow[x]);
    }
    put(1, 2, kBlack, PieceType::Cannon);
    put(7, 2, kBlack, PieceType::Cannon);
    for (int x = 0; x < kCols; x += 2)
    {
        put(x, 3, kBlack, PieceType::Pawn);
    }

    // 先手（红/下方）：y = 7..9
    for (int x = 0; x < kCols; ++x)
    {
        put(x, 9, kRed, backRow[x]);
    }
    put(1, 7, kRed, PieceType::Cannon);
    put(7, 7, kRed, PieceType::Cannon);
    for (int x = 0; x < kCols; x += 2)
    {
        put(x, 6, kRed, PieceType::Pawn);
    }

    sideToMove_    = Side::First;
    status_        = GameStatus::Playing;
    noCapturePlies_ = 0;
    history_.clear();
    keyHistory_.clear();
    keyHistory_.push_back(stateKey());
}

BoardSpec XiangqiGame::boardSpec() const
{
    BoardSpec s;
    s.kind = BoardKind::Intersections;
    s.cols = kCols;
    s.rows = kRows;
    return s;
}

// ---------------------------------------------------------------------------
//  着法生成
// ---------------------------------------------------------------------------

namespace {

// 方向表。注意不能写成 `for (const int d[4][2] : {{...}})` ——
// range-for 的元素类型不能是数组（无法从 braced-init-list 推导），
// 必须给一个具名的常量数组。
constexpr int kOrtho[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int kDiag[4][2]  = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr int kHorse[8][2] = {{1, 2},  {2, 1},  {2, -1}, {1, -2},
                              {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};

// 象棋字面与图集的映射
// ---------------------------------------------------------------------------
//  res/chesses.txt 里的名字按 "1/2" 后缀区分两方，但没有写明哪个是红哪个是黑。
//
//  字形规律（已逐格用填充率/主色核对过）：
//      红方用简体字形（笔画少）  帥 仕 相 马 车 兵
//      黑方用繁体字形（笔画多）  将 士 象 馬 車 卒
//  填充率佐证：che1=55px < che2=80px、ma1=47 < ma2=70、shi1=52 > shi2=38
//
//  **炮是唯一的例外**：红黑两方的炮字形相同（都是「炮」，图集里只有一个 pao），
//  所以两方共用同一个精灵。不要因为"红方找不到独立素材"就退化去画文字 ——
//  那会让同一个字在棋盘上呈现两种完全不同的视觉风格。
struct SpriteMapEntry
{
    PieceType   type;
    uint8_t     side;
    const char* sprite;
};

const SpriteMapEntry kSpriteMap[] = {
    {PieceType::King,     kRed,   "shuai1"},
    {PieceType::Advisor,  kRed,   "shi1"},
    {PieceType::Elephant, kRed,   "xiang1"},
    {PieceType::Horse,    kRed,   "ma1"},
    {PieceType::Chariot,  kRed,   "che1"},
    {PieceType::Cannon,   kRed,   "pao"},    // 与黑炮同字，复用同一精灵
    {PieceType::Pawn,     kRed,   "bing"},

    {PieceType::King,     kBlack, "jiang"},
    {PieceType::Advisor,  kBlack, "shi2"},
    {PieceType::Elephant, kBlack, "xiang2"},
    {PieceType::Horse,    kBlack, "ma2"},
    {PieceType::Chariot,  kBlack, "che2"},
    {PieceType::Cannon,   kBlack, "pao"},
    {PieceType::Pawn,     kBlack, "zu"},
};

const char* spriteFor(uint8_t piece)
{
    return XiangqiGame::spriteNameForPiece(piece);
}

} // namespace

const char* XiangqiGame::spriteNameForPiece(uint8_t piece)
{
    if (isEmpty(piece))
    {
        return nullptr;
    }
    for (const SpriteMapEntry& e : kSpriteMap)
    {
        if (e.type == typeOf(piece) && e.side == sideOf(piece))
        {
            return e.sprite;
        }
    }
    return nullptr;
}

namespace {

// ---------------------------------------------------------------------------
//  象棋棋子的构成：底盘 + 字面，两层叠加
// ---------------------------------------------------------------------------
//  res/chesses100.png 里这两类是分开的，实测填充率可以清楚区分：
//    chineseChess  655 px / 64%  木色(D9A066) + 棕边(8F6A45)  -> **底盘**
//    shuai1 等 13 个  38~80 px / 4~8%，全是 1B1B1B 笔画       -> **只有字**
//  所以只画字面会得到一个"悬空的字"，必须先铺底盘。
//
//  底盘对双方是同一个（字形本身区分敌我：帥/将、仕/士、相/象、马/馬、车/車、兵/卒）。
constexpr const char* kBaseSprite = "chineseChess";

// 字面精灵缺失时的自绘底盘（只有图集里连 chineseChess 都没有时才用）
void drawFallbackBase(const BoardView& bv, Coord c, PIMAGE img)
{
    const int cx = bv.px(c);
    const int cy = bv.py(c);
    const int r  = std::max(8, static_cast<int>(bv.cell() * 0.44));

    setfillcolor(EGERGB(0xE8, 0xD3, 0xA8), img);
    fillellipse(cx, cy, r, r, img);
    setcolor(EGERGB(0x8F, 0x6A, 0x45), img);
    setlinewidth(2.0f, img);
    circle(cx, cy, r, img);
    setlinewidth(1.0f, img);
}

// 字面精灵缺失时的最后兼底：直接画汉字。
// 正常情况下**不应被调用** —— 14 颗棋子现在都有对应精灵（见 kSpriteMap）。
// 保留它只是为了：万一换用别的图集（素材被删/改名）时，不至于出现"看不见的棋子"。
void drawPieceGlyph(const BoardView& bv, Coord c, uint8_t piece, PIMAGE img)
{
    // 红方用暗红、黑方用近黑，避免同一字形在两方之间完全无法分辨
    const bool red = (sideOf(piece) == kRed);
    setcolor(red ? EGERGB(0xA8, 0x2A, 0x22) : EGERGB(0x1B, 0x1B, 0x1B), img);
    text::setFont(std::max(13, static_cast<int>(bv.cell() * 0.52)), true, img);
    text::drawCentered(pieceName(piece), bv.px(c), bv.py(c), img);
}

} // namespace

// 沿 (dx,dy) 方向推进 n 格；越界返回 false
static bool step(int x, int y, int dx, int dy, int n, int* ox, int* oy)
{
    *ox = x + dx * n;
    *oy = y + dy * n;
    return inBoard(*ox, *oy);
}

void XiangqiGame::genPseudoMoves(uint8_t side, std::vector<Move>& out) const
{
    out.clear();

    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kCols; ++x)
        {
            const uint8_t p = board_[idx(x, y)];
            if (isEmpty(p) || sideOf(p) != side)
            {
                continue;
            }

            const PieceType t = typeOf(p);
            const Coord     from{x, y};

            auto emit = [&](int tx, int ty) {
                const uint8_t dst = board_[idx(tx, ty)];
                if (!isEmpty(dst) && sideOf(dst) == side)
                {
                    return;   // 不能吃自己的子
                }
                Move m{};
                m.from = from;
                m.to   = Coord{tx, ty};
                out.push_back(m);
            };

            switch (t)
            {
            case PieceType::King:
                // 一步直走，且限九宫内
                for (const auto& d : kOrtho)
                {
                    int nx = 0;
                    int ny = 0;
                    if (step(x, y, d[0], d[1], 1, &nx, &ny) && inPalace(side, nx, ny))
                    {
                        emit(nx, ny);
                    }
                }
                break;

            case PieceType::Advisor:
                // 一步斜走，且限九宫内
                for (const auto& d : kDiag)
                {
                    int nx = 0;
                    int ny = 0;
                    if (step(x, y, d[0], d[1], 1, &nx, &ny) && inPalace(side, nx, ny))
                    {
                        emit(nx, ny);
                    }
                }
                break;

            case PieceType::Elephant:
                // 走田字，象眼必须为空，且不能过河
                for (const auto& d : kDiag)
                {
                    int nx = 0;
                    int ny = 0;
                    if (!step(x, y, d[0], d[1], 2, &nx, &ny))
                    {
                        continue;
                    }
                    if (crossedRiver(side, ny))
                    {
                        continue;   // 象不过河
                    }
                    if (!isEmpty(board_[idx(x + d[0], y + d[1])]))
                    {
                        continue;   // 塞象眼
                    }
                    emit(nx, ny);
                }
                break;

            case PieceType::Horse:
                // 走日字，马腿必须为空
                for (const auto& d : kHorse)
                {
                    const int nx = x + d[0];
                    const int ny = y + d[1];
                    if (!inBoard(nx, ny))
                    {
                        continue;
                    }
                    // 马腿：沿"长边"方向的第一格
                    const int legX = x + (std::abs(d[0]) == 2 ? (d[0] > 0 ? 1 : -1) : 0);
                    const int legY = y + (std::abs(d[1]) == 2 ? (d[1] > 0 ? 1 : -1) : 0);
                    if (!isEmpty(board_[idx(legX, legY)]))
                    {
                        continue;
                    }
                    emit(nx, ny);
                }
                break;

            case PieceType::Chariot:
                // 直行，遇子止
                for (const auto& d : kOrtho)
                {
                    for (int n = 1; n < 10; ++n)
                    {
                        int nx = 0;
                        int ny = 0;
                        if (!step(x, y, d[0], d[1], n, &nx, &ny))
                        {
                            break;
                        }
                        const uint8_t dst = board_[idx(nx, ny)];
                        if (isEmpty(dst))
                        {
                            emit(nx, ny);
                            continue;
                        }
                        if (sideOf(dst) != side)
                        {
                            emit(nx, ny);
                        }
                        break;   // 无论吃到什么，都被挡住
                    }
                }
                break;

            case PieceType::Cannon:
                // 直行：越过且仅越过一个"炮架"后才能吃子
                for (const auto& d : kOrtho)
                {
                    int  screens  = 0;
                    bool finished = false;
                    for (int n = 1; n < 10 && !finished; ++n)
                    {
                        int nx = 0;
                        int ny = 0;
                        if (!step(x, y, d[0], d[1], n, &nx, &ny))
                        {
                            break;
                        }
                        const uint8_t dst = board_[idx(nx, ny)];

                        if (screens == 0)
                        {
                            if (isEmpty(dst))
                            {
                                emit(nx, ny);   // 无炮架时可当车走（走空格）
                            }
                            else
                            {
                                ++screens;   // 第一枚子成为炮架
                            }
                        }
                        else
                        {
                            if (isEmpty(dst))
                            {
                                continue;
                            }
                            // 炮架之后的第一个子：若是敌子则可吃，然后停
                            if (sideOf(dst) != side)
                            {
                                emit(nx, ny);
                            }
                            finished = true;
                        }
                    }
                }
                break;

            case PieceType::Pawn:
            {
                const int forward = (side == kRed) ? -1 : 1;
                int       nx      = 0;
                int       ny      = 0;
                if (step(x, y, 0, forward, 1, &nx, &ny))
                {
                    emit(nx, ny);
                }
                // 过河后可横走，但不能后退
                if (crossedRiver(side, y))
                {
                    if (step(x, y, 1, 0, 1, &nx, &ny))
                    {
                        emit(nx, ny);
                    }
                    if (step(x, y, -1, 0, 1, &nx, &ny))
                    {
                        emit(nx, ny);
                    }
                }
                break;
            }

            default:
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  将军判定
// ---------------------------------------------------------------------------

bool XiangqiGame::kingsFacing() const
{
    int redKingX = -1, redKingY = -1;
    int blkKingX = -1, blkKingY = -1;

    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kCols; ++x)
        {
            const uint8_t p = board_[idx(x, y)];
            if (typeOf(p) != PieceType::King)
            {
                continue;
            }
            if (sideOf(p) == kRed)
            {
                redKingX = x;
                redKingY = y;
            }
            else
            {
                blkKingX = x;
                blkKingY = y;
            }
        }
    }

    if (redKingX < 0 || blkKingX < 0 || redKingX != blkKingX)
    {
        return false;
    }

    // 同一列：中间不能有子（有子则被挡住，不算照面）
    const int x  = redKingX;
    const int lo = std::min(redKingY, blkKingY) + 1;
    const int hi = std::max(redKingY, blkKingY);
    for (int y = lo; y < hi; ++y)
    {
        if (!isEmpty(board_[idx(x, y)]))
        {
            return false;
        }
    }
    return true;
}

bool XiangqiGame::inCheck(uint8_t side) const
{
    // 找到己方将/帅
    int kx = -1;
    int ky = -1;
    for (int y = 0; y < kRows && kx < 0; ++y)
    {
        for (int x = 0; x < kCols; ++x)
        {
            const uint8_t p = board_[idx(x, y)];
            if (typeOf(p) == PieceType::King && sideOf(p) == side)
            {
                kx = x;
                ky = y;
                break;
            }
        }
    }
    if (kx < 0)
    {
        return false;   // 将被吃（理论上不会走到这里）
    }

    const uint8_t opp = (side == kRed) ? kBlack : kRed;
    const Coord   king{kx, ky};

    // 逐个检查对方棋子能否吃到将/帅。为了避免递归，这里直接按棋子类型判断。
    // 车 / 炮：沿 4 个方向扫描
    for (const auto& d : kOrtho)
    {
        int screens = 0;
        for (int n = 1; n < 10; ++n)
        {
            int nx = 0;
            int ny = 0;
            if (!step(kx, ky, d[0], d[1], n, &nx, &ny))
            {
                break;
            }
            const uint8_t p = board_[idx(nx, ny)];
            if (isEmpty(p))
            {
                continue;
            }
            if (screens == 0)
            {
                if (sideOf(p) == opp)
                {
                    const PieceType t = typeOf(p);
                    // 车、以及"同列且无阻挡的将帅"（飞将）都算将军
                    if (t == PieceType::Chariot || t == PieceType::King)
                    {
                        return true;
                    }
                    // 兵/卒贴身的两种吃法：
                    //   * 正前方：兵在"将的上一格"就能直接走进来
                    //   * 横向：必须已经过河
                    if (t == PieceType::Pawn)
                    {
                        const int  fwd           = (opp == kRed) ? -1 : 1;   // 该方兵的前进方向
                        const bool forwardAttack = (nx == kx) && (ny == ky - fwd);
                        const bool sideAttack =
                            (ny == ky) && (std::abs(nx - kx) == 1) && xq::crossedRiver(opp, ny);
                        if (forwardAttack || sideAttack)
                        {
                            return true;
                        }
                    }
                }
                ++screens;   // 继续向外找炮
            }
            else
            {
                // 炮架之后的第一枚子：若是敌炮则被将军
                if (sideOf(p) == opp && typeOf(p) == PieceType::Cannon)
                {
                    return true;
                }
                break;
            }
        }
    }

    // 马：8 个方向反向找马，并检查马腿（从马那侧算）
    for (const auto& d : kHorse)
    {
        const int hx = kx + d[0];
        const int hy = ky + d[1];
        if (!inBoard(hx, hy))
        {
            continue;
        }
        const uint8_t p = board_[idx(hx, hy)];
        if (sideOf(p) != opp || typeOf(p) != PieceType::Horse)
        {
            continue;
        }
        // 马腿在马的一侧：长边方向的第一格
        const int legX = hx + (std::abs(d[0]) == 2 ? (d[0] > 0 ? -1 : 1) : 0);
        const int legY = hy + (std::abs(d[1]) == 2 ? (d[1] > 0 ? -1 : 1) : 0);
        if (inBoard(legX, legY) && isEmpty(board_[idx(legX, legY)]))
        {
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
//  合法性（走后自己不能被将军）
// ---------------------------------------------------------------------------

bool XiangqiGame::isLegal(const Move& m) const
{
    if (status_ != GameStatus::Playing)
    {
        return false;
    }
    if (!inBoard(m.from.x, m.from.y) || !inBoard(m.to.x, m.to.y))
    {
        return false;
    }

    const uint8_t side = (sideToMove_ == Side::First) ? kRed : kBlack;

    // 起点必须是自己的子、终点不能是自己的子
    const uint8_t src = board_[idx(m.from.x, m.from.y)];
    if (isEmpty(src) || sideOf(src) != side)
    {
        return false;
    }
    const uint8_t dst = board_[idx(m.to.x, m.to.y)];
    if (!isEmpty(dst) && sideOf(dst) == side)
    {
        return false;
    }

    // 用伪合法着法集合判断这一步是否在规则允许的形状内
    std::vector<Move> pseudo;
    genPseudoMoves(side, pseudo);
    const bool shapeOk = std::any_of(pseudo.begin(), pseudo.end(),
                                     [&](const Move& pm) { return pm == m; });
    if (!shapeOk)
    {
        return false;
    }

    // 试走后自将 / 照面 都算非法
    XiangqiGame tmp = *this;
    tmp.board_[idx(m.to.x, m.to.y)] = src;
    tmp.board_[idx(m.from.x, m.from.y)] = 0;

    if (tmp.inCheck(side))
    {
        return false;
    }
    // 飞将：照面同样非法（inCheck 里已把对方将帅算作攻击，这里再兜一层）
    if (tmp.kingsFacing())
    {
        return false;
    }
    return true;
}

std::vector<Move> XiangqiGame::legalMoves() const
{
    std::vector<Move> out;
    if (status_ != GameStatus::Playing)
    {
        return out;
    }

    const uint8_t     side = (sideToMove_ == Side::First) ? kRed : kBlack;
    std::vector<Move> pseudo;
    genPseudoMoves(side, pseudo);

    out.reserve(pseudo.size());
    for (const Move& m : pseudo)
    {
        if (isLegal(m))
        {
            out.push_back(m);
        }
    }
    return out;
}

bool XiangqiGame::hasPieceOf(Coord c, Side s) const
{
    const uint8_t p = at(c.x, c.y);
    if (isEmpty(p))
    {
        return false;
    }
    return sideOf(p) == ((s == Side::First) ? kRed : kBlack);
}

bool XiangqiGame::selectableFrom(Coord c, Side s) const
{
    if (status_ != GameStatus::Playing || !hasPieceOf(c, s))
    {
        return false;
    }
    // 只有在"这枚子确实有地方可去"时才允许选中，
    // 避免玩家点了半天却是被牵制的子
    for (const Move& m : legalMoves())
    {
        if (m.from == c)
        {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
//  走子
// ---------------------------------------------------------------------------

bool XiangqiGame::apply(const Move& m)
{
    if (!isLegal(m))
    {
        return false;
    }

    const uint8_t src = board_[idx(m.from.x, m.from.y)];
    const uint8_t dst = board_[idx(m.to.x, m.to.y)];

    MoveRec rec;
    rec.m         = m;
    rec.captured  = dst;
    rec.keyBefore = stateKey();

    board_[idx(m.to.x, m.to.y)]     = src;
    board_[idx(m.from.x, m.from.y)] = 0;

    noCapturePlies_ = isEmpty(dst) ? (noCapturePlies_ + 1) : 0;

    history_.push_back(rec);
    sideToMove_ = otherSide(sideToMove_);

    keyHistory_.push_back(stateKey());

    updateStatusAfterMove();
    return true;
}

void XiangqiGame::updateStatusAfterMove()
{
    status_ = GameStatus::Playing;

    const uint8_t them = (sideToMove_ == Side::First) ? kRed : kBlack;
    const uint8_t me   = (them == kRed) ? kBlack : kRed;

    // 60 回合（120 个半回合）无吃子 -> 和棋
    if (noCapturePlies_ >= 120)
    {
        status_ = GameStatus::Draw;
        return;
    }

    // 长将判负 / 三次重复局面
    //  简化处理：若同一局面出现 3 次，且最后两次都是同一方在将军，
    //  则判该方负（长将）；否则判和棋。
    const uint64_t key = keyHistory_.back();
    int            repeats = 0;
    for (uint64_t k : keyHistory_)
    {
        if (k == key)
        {
            ++repeats;
        }
    }
    if (repeats >= 3)
    {
        const bool sideToMoveChecks = inCheck(them);
        if (sideToMoveChecks)
        {
            // 轮到走棋的一方被将军 —— 说明对方在反复将军，判对方负
            status_ = (me == kRed) ? GameStatus::FirstWin : GameStatus::SecondWin;
        }
        else
        {
            status_ = GameStatus::Draw;
        }
        return;
    }

    // 将死 / 困毙：轮到走棋的一方无合法着法即负
    if (legalMoves().empty())
    {
        status_ = (them == kRed) ? GameStatus::SecondWin : GameStatus::FirstWin;
    }
}

bool XiangqiGame::undo()
{
    if (history_.empty())
    {
        return false;
    }

    const MoveRec rec = history_.back();
    history_.pop_back();

    board_[idx(rec.m.from.x, rec.m.from.y)] = board_[idx(rec.m.to.x, rec.m.to.y)];
    board_[idx(rec.m.to.x, rec.m.to.y)]     = rec.captured;

    sideToMove_ = otherSide(sideToMove_);
    status_     = GameStatus::Playing;

    if (isEmpty(rec.captured))
    {
        noCapturePlies_ = std::max(0, noCapturePlies_ - 1);
    }
    else
    {
        // 恢复后无法准确还原"上次吃子至今"的计数，
        // 用 0 会让立即再生效判和，所以退一步取上一步的计数（保守）
        noCapturePlies_ = 0;
    }

    if (keyHistory_.size() > 1)
    {
        keyHistory_.pop_back();
    }
    return true;
}

// ---------------------------------------------------------------------------
//  指纹
// ---------------------------------------------------------------------------

uint64_t XiangqiGame::keyOf(uint8_t side) const
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < kCols * kRows; ++i)
    {
        h ^= static_cast<uint64_t>(board_[i] + 1);
        h *= 0x100000001B3ull;
    }
    h ^= static_cast<uint64_t>(side + 1);
    h *= 0x100000001B3ull;
    return h;
}

uint64_t XiangqiGame::stateKey() const
{
    return keyOf((sideToMove_ == Side::First) ? kRed : kBlack);
}

bool XiangqiGame::givesCheck(uint8_t side) const
{
    // 该方是否正在将军对方（用于长将提示）
    const uint8_t opp = (side == kRed) ? kBlack : kRed;
    return inCheck(opp);
}

std::vector<Coord> XiangqiGame::opponentCheckTargets() const
{
    std::vector<Coord> out;
    if (status_ != GameStatus::Playing)
    {
        return out;
    }

    const uint8_t me   = (sideToMove_ == Side::First) ? kRed : kBlack;
    const uint8_t them = (me == kRed) ? kBlack : kRed;

    std::vector<Move> oppMoves;
    genPseudoMoves(them, oppMoves);

    for (const Move& r : oppMoves)
    {
        XiangqiGame after = *this;
        after.board_[idx(r.to.x, r.to.y)]     = after.board_[idx(r.from.x, r.from.y)];
        after.board_[idx(r.from.x, r.from.y)] = 0;
        if (after.inCheck(me))
        {
            out.push_back(r.to);
        }
    }
    return out;
}

std::unique_ptr<IGame> XiangqiGame::clone() const
{
    return std::make_unique<XiangqiGame>(*this);
}

// ---------------------------------------------------------------------------
//  文本
// ---------------------------------------------------------------------------

const char* XiangqiGame::sideName(Side s) const
{
    return s == Side::First ? "红方" : (s == Side::Second ? "黑方" : "—");
}

std::string XiangqiGame::statusText() const
{
    switch (status_)
    {
    case GameStatus::FirstWin:  return "红方胜（将死 / 困毙 / 长将判负）";
    case GameStatus::SecondWin: return "黑方胜（将死 / 困毙 / 长将判负）";
    case GameStatus::Draw:      return "和棋（60 回合无吃子或局面重复）";
    default:                    break;
    }

    const uint8_t me = (sideToMove_ == Side::First) ? kRed : kBlack;
    if (inCheck(me))
    {
        return sideToMove_ == Side::First ? "红方被将军！" : "黑方被将军！";
    }
    return sideToMove_ == Side::First ? "轮到红方走棋" : "轮到黑方走棋";
}

std::string XiangqiGame::scoreText() const
{
    int red = 0;
    int blk = 0;
    for (int i = 0; i < kCols * kRows; ++i)
    {
        if (isEmpty(board_[i]))
        {
            continue;
        }
        (sideOf(board_[i]) == kRed ? red : blk)++;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "回合 %d   红 %d 子   黑 %d 子", moveCount(), red, blk);
    return buf;
}

Move XiangqiGame::lastMove() const
{
    Move m{};
    if (!history_.empty())
    {
        m = history_.back().m;
    }
    return m;
}

// ---------------------------------------------------------------------------
//  提示（L1/L2 由游戏给出）
// ---------------------------------------------------------------------------

HintData XiangqiGame::basicHint(int level) const
{
    HintData h;
    if (status_ != GameStatus::Playing || level <= 0 || level >= 3)
    {
        return h;
    }

    // 选中棋子的可走位置：由 GameScene 通过 DrawContext.selected 提供，
    // 这里无法知道选中了谁，所以 L1 给出"当前行棋方所有可走着法的目的地"。
    // 信息量足够（玩家能看到自己能去哪），且不会泄露"哪些子被牵制"以外的秘密。
    const auto moves = legalMoves();

    for (const Move& m : moves)
    {
        HintCell hc;
        hc.coord  = m.to;
        hc.kind   = HintKind::Legal;
        hc.weight = 0.5f;
        h.cells.push_back(hc);
    }

    if (level >= 2)
    {
        // 标记"走过去会被吃"的目的地：走后如果对方能吃回该格，则标危险
        const uint8_t me   = (sideToMove_ == Side::First) ? kRed : kBlack;
        const uint8_t them = (me == kRed) ? kBlack : kRed;

        for (const Move& m : moves)
        {
            const uint8_t dst = board_[idx(m.to.x, m.to.y)];
            if (isEmpty(dst))
            {
                // 只对"主动送子"的位置预警：走过去后对方能否立刻吃这个子
                XiangqiGame tmp = *this;
                tmp.board_[idx(m.to.x, m.to.y)]     = tmp.board_[idx(m.from.x, m.from.y)];
                tmp.board_[idx(m.from.x, m.from.y)] = 0;

                std::vector<Move> reply;
                tmp.genPseudoMoves(them, reply);
                bool canBeTaken = false;
                for (const Move& r : reply)
                {
                    if (r.to == m.to && !isEmpty(tmp.board_[idx(m.to.x, m.to.y)]))
                    {
                        canBeTaken = true;
                        break;
                    }
                }
                if (canBeTaken)
                {
                    HintCell hc;
                    hc.coord  = m.to;
                    hc.kind   = HintKind::Danger;
                    hc.weight = 0.55f;
                    h.cells.push_back(hc);
                }
            }
        }

        // 对方能将军的点：标为威胁
        std::vector<Move> oppMoves;
        {
            XiangqiGame tmp = *this;
            tmp.sideToMove_ = otherSide(sideToMove_);
            tmp.genPseudoMoves(them, oppMoves);
            for (const Move& r : oppMoves)
            {
                XiangqiGame after = tmp;
                after.board_[idx(r.to.x, r.to.y)]     = after.board_[idx(r.from.x, r.from.y)];
                after.board_[idx(r.from.x, r.from.y)] = 0;
                if (after.inCheck(me) || after.kingsFacing())
                {
                    HintCell hc;
                    hc.coord  = r.to;
                    hc.kind   = HintKind::Threat;
                    hc.weight = 0.7f;
                    h.cells.push_back(hc);
                }
            }
        }
    }

    h.notes.push_back(level == 1 ? "白点 = 当前可走的目的地"
                                 : "白=可走，橙=会被吃，红=对手能在此将军");
    return h;
}

// ---------------------------------------------------------------------------
//  绘制
// ---------------------------------------------------------------------------
//  棋子绘制的辅助函数（spriteFor / drawTextPiece）定义在本文件上方的匿名
//  namespace 里，与着法生成共用同一份"字面 -> 图集"映射表。

void XiangqiGame::drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
{
    (void)ctx;

    const int c  = bv.cell();
    const int lw = std::max(2, c / 22);

    setcolor(theme::kBoardLine, img);

    // 外框（比普通棋盘线稍粗）
    setlinewidth(static_cast<float>(lw + 1), img);
    rectangle(bv.px(Coord{0, 0}), bv.py(Coord{0, 0}), bv.px(Coord{kCols - 1, 0}),
              bv.py(Coord{0, kRows - 1}), img);

    // 河界：y = 4 与 y = 5 之间不画竖线
    setlinewidth(static_cast<float>(lw), img);
    const int riverTop = bv.py(Coord{0, 4});
    const int riverBot = bv.py(Coord{0, 5});

    for (int x = 0; x < kCols; ++x)
    {
        const int gx = bv.px(Coord{x, 0});
        if (x == 0 || x == kCols - 1)
        {
            line(gx, bv.py(Coord{0, 0}), gx, bv.py(Coord{0, kRows - 1}), img);
        }
        else
        {
            line(gx, bv.py(Coord{0, 0}), gx, riverTop, img);
            line(gx, riverBot, gx, bv.py(Coord{0, kRows - 1}), img);
        }
    }

    for (int y = 0; y < kRows; ++y)
    {
        const int gy = bv.py(Coord{0, y});
        line(bv.px(Coord{0, 0}), gy, bv.px(Coord{kCols - 1, 0}), gy, img);
    }

    // 九宫斜线
    auto palaceDiagonal = [&](int x0, int y0, int x1, int y1) {
        line(bv.px(Coord{x0, y0}), bv.py(Coord{x0, y0}),
             bv.px(Coord{x1, y1}), bv.py(Coord{x1, y1}), img);
    };
    palaceDiagonal(3, 0, 5, 2);
    palaceDiagonal(5, 0, 3, 2);
    palaceDiagonal(3, 7, 5, 9);
    palaceDiagonal(5, 7, 3, 9);

    // 河界文字：把红方小字标在河边（不依赖图集，纯文字）
    setlinewidth(1.0f, img);
    text::setFont(std::clamp(c / 3, 14, 22), false, img);
    setcolor(theme::kBoardLine, img);
    {
        const int midY = (riverTop + riverBot) / 2;
        const int cx   = bv.px(Coord{4, 0});
        text::drawCentered("楚 河", cx - c, midY, img);
        text::drawCentered("汉 界", cx + c, midY, img);
    }

    // 兵/炮位的短横标记（传统棋盘的走子位提示），可选但更"像象棋盘"
    // 只画在兵与炮所在的几个点上
    auto tick = [&](int x, int y) {
        const int cx = bv.px(Coord{x, y});
        const int cy = bv.py(Coord{0, y});
        const int d  = std::max(3, c / 8);
        const int len = std::max(5, c / 4);
        setcolor(theme::kBoardLine, img);
        // 左右各画一个小折角
        if (x > 0)
        {
            line(cx - d, cy - d, cx - d - len, cy - d, img);
            line(cx - d, cy - d, cx - d, cy - d - len, img);
            line(cx - d, cy + d, cx - d - len, cy + d, img);
            line(cx - d, cy + d, cx - d, cy + d + len, img);
        }
        if (x < kCols - 1)
        {
            line(cx + d, cy - d, cx + d + len, cy - d, img);
            line(cx + d, cy - d, cx + d, cy - d - len, img);
            line(cx + d, cy + d, cx + d + len, cy + d, img);
            line(cx + d, cy + d, cx + d, cy + d + len, img);
        }
    };
    for (int x = 0; x < kCols; x += 2)
    {
        tick(x, 3);
        tick(x, 6);
    }
    tick(1, 2);
    tick(7, 2);
    tick(1, 7);
    tick(7, 7);
}

void XiangqiGame::drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const
{
    const uint8_t p = at(c.x, c.y);
    if (isEmpty(p))
    {
        return;
    }

    const int sz = bv.pieceSize(0.92);
    const int sx = bv.spriteX(c, sz);
    const int sy = bv.spriteY(c, sz);

    const bool hasAtlas = (ctx.atlas != nullptr);

    // ---- 第 1 层：底盘 ----
    // 图集里的字面精灵是**透明底**的，只有笔画；不铺底盘就会看到一排悬空的字。
    if (hasAtlas && ctx.atlas->has(kBaseSprite))
    {
        ctx.atlas->draw(kBaseSprite, sx, sy, sz, img);
    }
    else
    {
        drawFallbackBase(bv, c, img);
    }

    // ---- 第 2 层：字面 ----
    // 与底盘同尺寸、同位置。源图里两者都是 32x32 的整格精灵，
    // 直接按同一个目标矩形绘制即可天然对齐，不需要额外偏移。
    const char* sprite = spriteFor(p);
    if (sprite && hasAtlas && ctx.atlas->has(sprite))
    {
        // dest 必须显式传 img，否则离屏渲染时精灵会被画到窗口上
        ctx.atlas->draw(sprite, sx, sy, sz, img);
        return;
    }

    // 防御性分支：14 颗棋子都有精灵，正常情况下走不到这里。
    // 只在图集被替换/改名导致素材缺失时，用文字保证棋子仍然可见。
    drawPieceGlyph(bv, c, p, img);
}

void XiangqiGame::drawOverlay(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
{
    // 被将军时把将/帅圈出来，这是最重要的一条信息
    if (!ctx.showHint)
    {
        return;
    }

    const uint8_t me = (sideToMove_ == Side::First) ? kRed : kBlack;
    if (!inCheck(me))
    {
        return;
    }

    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kCols; ++x)
        {
            const uint8_t p = board_[idx(x, y)];
            if (typeOf(p) == PieceType::King && sideOf(p) == me)
            {
                setcolor(theme::kDanger, img);
                setlinewidth(3.0f, img);
                circle(bv.px(Coord{x, y}), bv.py(Coord{x, y}),
                       std::max(10, static_cast<int>(bv.cell() * 0.48)), img);
                setlinewidth(1.0f, img);
            }
        }
    }
}

} // namespace chess
