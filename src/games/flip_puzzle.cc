#include "games/flip_puzzle.h"

#include "ai/gf2.h"
#include "games/registry.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>

namespace chess {
namespace flip {

void applyPress(std::vector<Cell>& board, int cols, int rows, int x, int y)
{
    if (x < 0 || y < 0 || x >= cols || y >= rows)
    {
        return;
    }

    // 该格 + 上下左右
    const int dx[5] = {0, 0, 0, -1, 1};
    const int dy[5] = {0, -1, 1, 0, 0};
    for (int k = 0; k < 5; ++k)
    {
        const int nx = x + dx[k];
        const int ny = y + dy[k];
        if (nx < 0 || ny < 0 || nx >= cols || ny >= rows)
        {
            continue;
        }
        Cell& c = board[indexOf(nx, ny, cols)];
        c = (c == kBlack) ? kWhite : kBlack;
    }
}

std::pair<int, int> countColors(const std::vector<Cell>& board)
{
    int black = 0;
    for (Cell c : board)
    {
        if (c == kBlack)
        {
            ++black;
        }
    }
    return {black, static_cast<int>(board.size()) - black};
}

Solution solveTo(const std::vector<Cell>& board, int cols, int rows, Cell target)
{
    Solution sol;

    const int n = cols * rows;
    if (static_cast<int>(board.size()) != n || n <= 0)
    {
        return sol;
    }

    // 变量 = 每个格子"按或不按"；方程 = 每个格子的最终颜色要求
    //   对格子 j： sum(x_i for i in {j, 上下左右}) == (board[j] != target)
    Gf2System sys;
    sys.init(n, n);

    for (int y = 0; y < rows; ++y)
    {
        for (int x = 0; x < cols; ++x)
        {
            const int eq = indexOf(x, y, cols);

            const int dx[5] = {0, 0, 0, -1, 1};
            const int dy[5] = {0, -1, 1, 0, 0};
            for (int k = 0; k < 5; ++k)
            {
                const int nx = x + dx[k];
                const int ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= cols || ny >= rows)
                {
                    continue;
                }
                sys.set(eq, indexOf(nx, ny, cols), true);
            }

            // 该格当前颜色与目标不同 -> 需要被翻奇数次
            sys.setRhs(eq, board[eq] != target);
        }
    }

    std::vector<uint8_t> particular;
    int                  nullity = 0;
    if (!sys.solve(particular, &nullity))
    {
        return sol;   // 该目标不可达
    }

    const auto& basis = sys.nullBasis();

    // 全部解 = 特解 + 零空间的任意线性组合。
    // 枚举 2^nullity 个组合取按键最少者；nullity 通常很小（5x5 是 2），
    // 但为安全起见设一个上限，超过就只用特解。
    std::vector<uint8_t> best = particular;
    int                  bestWeight = 0;
    for (uint8_t v : best)
    {
        bestWeight += v;
    }

    constexpr int kMaxNullity = 16;
    if (nullity > 0 && nullity <= kMaxNullity)
    {
        const uint32_t combos = 1u << nullity;
        for (uint32_t mask = 1; mask < combos; ++mask)
        {
            std::vector<uint8_t> cand = particular;
            int                  weight = 0;

            for (int b = 0; b < nullity; ++b)
            {
                if (!(mask & (1u << b)))
                {
                    continue;
                }
                const std::vector<uint8_t>& vec = basis[b];
                for (int i = 0; i < n; ++i)
                {
                    cand[i] ^= vec[i];
                }
            }
            for (uint8_t v : cand)
            {
                weight += v;
            }

            if (weight < bestWeight)
            {
                bestWeight = weight;
                best       = std::move(cand);
            }
        }
    }

    sol.found      = true;
    sol.press      = best;
    sol.pressCount = bestWeight;
    sol.nullity    = nullity;

    // "所有解都必须按"的格子：把每个变量在所有解上的取值都算一遍。
    // 某个变量在零空间所有基向量上都是 0，说明它被特解唯一确定。
    sol.required.assign(n, 0);
    for (int i = 0; i < n; ++i)
    {
        bool determined = true;
        for (const auto& vec : basis)
        {
            if (vec[i])
            {
                determined = false;
                break;
            }
        }
        if (determined)
        {
            sol.required[i] = particular[i];
        }
    }

    return sol;
}

Solution solveAny(const std::vector<Cell>& board, int cols, int rows)
{
    Solution toBlack = solveTo(board, cols, rows, kBlack);
    Solution toWhite = solveTo(board, cols, rows, kWhite);

    if (!toBlack.found)
    {
        return toWhite;
    }
    if (!toWhite.found)
    {
        return toBlack;
    }
    // 平局偏向"全白"：生成局面就是从全白出发的，视觉上更自然
    return (toWhite.pressCount <= toBlack.pressCount) ? toWhite : toBlack;
}

} // namespace flip

// ---------------------------------------------------------------------------
//  游戏实现
// ---------------------------------------------------------------------------

GameDesc FlipPuzzleGame::desc() const
{
    // 变体 = 难度（打乱步数），这是唯一有意义的难度旋钮
    static const std::vector<std::string> variants = {"简单", "普通", "困难"};

    static const std::vector<BoardPreset> boards = {
        {"4 x 4", 4, 4},
        {"5 x 5", 5, 5},
        {"6 x 6", 6, 6},
        {"8 x 8", 8, 8},
    };

    // 提示层级（与 registry 的默认说明保持一致，但换成这个游戏的语境）
    static const std::vector<LevelDesc> levels = {
        {"入门", "标出所有可以按的格子"},
        {"初级", "额外提示哪些按法会立刻变好 / 变差"},
        {"中级", "用精确求解器给出最佳一步与剩余步数"},
        {"高级", "给出完整的解法序列（按顺序点）"},
        {"大师", "给出剩余步数、必按格与解的数量"},
    };

    GameDesc d;
    d.id             = "flip";
    d.name           = "翻转棋";
    d.blurb          = "点一格翻它和上下左右共 5 格，把全盘翻成同一种颜色";
    d.variants       = variants;
    d.boardPresets   = boards;
    d.levels         = levels;
    d.create         = []() -> std::unique_ptr<IGame> { return std::make_unique<FlipPuzzleGame>(); };
    d.createAi       = nullptr;   // 在注册表里填（避免这里依赖 ai/ 层）
    return d;
}

void FlipPuzzleGame::setup(const GameConfig& cfg)
{
    cfg_ = cfg;

    cols_ = std::max(3, cfg.cols);
    rows_ = std::max(3, cfg.rows);

    static const char* kNames[3] = {"简单", "普通", "困难"};
    variantName_ = kNames[std::clamp(cfg.variant, 0, 2)];

    rng_.seed(cfg.seed != 0 ? cfg.seed : 0x5DEECE66Dull);

    scramble();
}

void FlipPuzzleGame::scramble()
{
    const int n = cols_ * rows_;

    // 打乱步数（候选）与"希望达到的最少步数"
    int presses   = 0;
    int targetMin = 0;
    switch (cfg_.variant)
    {
    case 0:
        presses   = std::max(2, n / 8);
        targetMin = 2;
        break;
    case 2:
        presses   = std::max(4, n / 3);
        targetMin = std::max(4, n / 6);
        break;
    default:
        presses   = std::max(3, n / 5);
        targetMin = std::max(3, n / 8);
        break;
    }

    // 关键：局面必须"从全同色出发按若干次"得到，这样必定可解
    //   —— 按键是自逆的，再按一遍同样的格子就回到全同色。
    //
    // 这件事不是多余的谨慎：Lights-Out 类的覆盖矩阵在部分尺寸上是**奇异**的
    // （例如 4x4 秩只有 12），任意随机的局面**大多数根本无解**。
    // 所以不能随机撒棋子，只能这样生成。
    //
    // 另外打乱 N 次 != 最少步数就是 N（按键会互相抵消），
    // 因此这里用求解器实测最少步数，挑一个真正够难的：
    // 先找满足目标难度的，找不到就取这些候选里最难的一个。
    std::vector<flip::Cell> best;
    int                     bestMin = -1;

    for (int attempt = 0; attempt < 24; ++attempt)
    {
        std::vector<flip::Cell> cand(n, flip::kWhite);

        // 越往后多按几次，提高撞到"真的难"的概率
        const int tries = presses + attempt / 4;
        for (int i = 0; i < tries; ++i)
        {
            flip::applyPress(cand, cols_, rows_, rng_.nextInt(0, cols_ - 1),
                             rng_.nextInt(0, rows_ - 1));
        }

        if (flip::potential(cand) == 0)
        {
            continue;   // 已经是全同色，不要
        }

        const auto sol = flip::solveAny(cand, cols_, rows_);
        if (!sol.found || sol.pressCount == 0)
        {
            continue;
        }

        if (sol.pressCount >= targetMin)
        {
            best    = std::move(cand);
            bestMin = sol.pressCount;
            break;
        }
        if (sol.pressCount > bestMin)
        {
            best    = std::move(cand);
            bestMin = sol.pressCount;
        }
    }

    if (best.empty())
    {
        // 兜底：极少数情况下（例如很小的盘）仍要保证局面可解且未完成
        best.assign(n, flip::kWhite);
        for (int i = 0; i < presses; ++i)
        {
            flip::applyPress(best, cols_, rows_, rng_.nextInt(0, cols_ - 1),
                             rng_.nextInt(0, rows_ - 1));
        }
    }

    cells_ = std::move(best);
    history_.clear();
    solveKey_ = ~0ull;
}

BoardSpec FlipPuzzleGame::boardSpec() const
{
    BoardSpec s;
    s.kind = BoardKind::CellGrid;   // 棋子落在格子里
    s.cols = cols_;
    s.rows = rows_;
    return s;
}

GameStatus FlipPuzzleGame::status() const
{
    return flip::potential(cells_) == 0 ? GameStatus::FirstWin : GameStatus::Playing;
}

std::vector<Move> FlipPuzzleGame::legalMoves() const
{
    std::vector<Move> out;
    out.reserve(static_cast<size_t>(cols_ * rows_));

    if (status() != GameStatus::Playing)
    {
        return out;
    }

    for (int y = 0; y < rows_; ++y)
    {
        for (int x = 0; x < cols_; ++x)
        {
            Move m{};
            m.to = Coord{x, y};
            out.push_back(m);
        }
    }
    return out;
}

bool FlipPuzzleGame::isLegal(const Move& m) const
{
    if (status() != GameStatus::Playing)
    {
        return false;
    }
    return m.to.x >= 0 && m.to.y >= 0 && m.to.x < cols_ && m.to.y < rows_;
}

bool FlipPuzzleGame::apply(const Move& m)
{
    if (!isLegal(m))
    {
        return false;
    }

    flip::applyPress(cells_, cols_, rows_, m.to.x, m.to.y);
    history_.push_back(m.to);
    return true;
}

bool FlipPuzzleGame::undo()
{
    if (history_.empty())
    {
        return false;
    }
    // 按键是自逆的：再按一次即撤销
    const Coord c = history_.back();
    history_.pop_back();
    flip::applyPress(cells_, cols_, rows_, c.x, c.y);
    return true;
}

uint64_t FlipPuzzleGame::stateKey() const
{
    // FNV-1a 变体：把每个格子的颜色揉进 64 位指纹
    uint64_t h = 0xCBF29CE484222325ull;
    h ^= static_cast<uint64_t>(cols_);
    h *= 0x100000001B3ull;
    h ^= static_cast<uint64_t>(rows_);
    h *= 0x100000001B3ull;
    for (flip::Cell c : cells_)
    {
        h ^= static_cast<uint64_t>(c + 1);
        h *= 0x100000001B3ull;
    }
    return h;
}

std::unique_ptr<IGame> FlipPuzzleGame::clone() const
{
    return std::make_unique<FlipPuzzleGame>(*this);
}

const char* FlipPuzzleGame::sideName(Side s) const
{
    return s == Side::First ? "黑棋" : (s == Side::Second ? "白棋" : "—");
}

std::string FlipPuzzleGame::statusText() const
{
    if (status() != GameStatus::Playing)
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "完成！共按了 %d 次", static_cast<int>(history_.size()));
        return buf;
    }

    const auto sol = solution();
    char       buf[160];
    if (sol.found)
    {
        std::snprintf(buf, sizeof(buf), "把全盘翻成同色 —— 最少还要 %d 步", sol.pressCount);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "把全盘翻成同色");
    }
    return buf;
}

std::string FlipPuzzleGame::scoreText() const
{
    const auto c = flip::countColors(cells_);
    char       buf[96];
    std::snprintf(buf, sizeof(buf), "黑 %d  白 %d  （总 %d）", c.first, c.second,
                  static_cast<int>(cells_.size()));
    return buf;
}

const flip::Solution& FlipPuzzleGame::solution() const
{
    const uint64_t key = stateKey();
    if (key != solveKey_)
    {
        solveCache_ = flip::solveAny(cells_, cols_, rows_);
        solveKey_   = key;
    }
    return solveCache_;
}

HintData FlipPuzzleGame::basicHint(int level) const
{
    HintData h;
    if (status() != GameStatus::Playing)
    {
        return h;
    }

    // 分层原则："信息更多"应该是"更有用"，而不是"更吵"。
    //   L1  所有可点格子（圆点）
    //   L2  追加贪心判断（按了变近/变远）
    //   L3+ 完全交给搜索：只给推荐点，不再铺满整盘的贪心标记
    if (level >= 3)
    {
        return h;
    }

    if (level == 1)
    {
        for (int y = 0; y < rows_; ++y)
        {
            for (int x = 0; x < cols_; ++x)
            {
                HintCell hc;
                hc.coord  = Coord{x, y};
                hc.kind   = HintKind::Legal;
                hc.weight = 0.4f;
                h.cells.push_back(hc);
            }
        }
        return h;
    }

    // ---- L2：按了之后"离全同色更近/更远" ----
    const int cur = flip::potential(cells_);
    for (int y = 0; y < rows_; ++y)
    {
        for (int x = 0; x < cols_; ++x)
        {
            std::vector<flip::Cell> tmp = cells_;
            flip::applyPress(tmp, cols_, rows_, x, y);
            const int after = flip::potential(tmp);

            HintCell hc;
            hc.coord = Coord{x, y};
            if (after < cur)
            {
                hc.kind   = HintKind::Good;
                hc.weight = 0.6f;
            }
            else if (after > cur)
            {
                hc.kind   = HintKind::Danger;
                hc.weight = 0.5f;
            }
            else
            {
                hc.kind   = HintKind::Legal;
                hc.weight = 0.3f;
            }
            h.cells.push_back(hc);
        }
    }

    h.notes.push_back("绿=按了离全同色更近，橙=更远");
    return h;
}

void FlipPuzzleGame::drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
{
    // 棋盘上不加装饰，保持干净：目标与进度全部由 HUD 呈现
    (void)bv;
    (void)ctx;
    (void)img;
}

void FlipPuzzleGame::drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const
{
    if (c.x < 0 || c.y < 0 || c.x >= cols_ || c.y >= rows_)
    {
        return;
    }

    const flip::Cell cell = cells_[flip::indexOf(c.x, c.y, cols_)];
    const bool      black = (cell == flip::kBlack);

    // 棋子略小于格子，留出一圈木色
    const int sz = bv.pieceSize(0.9);

    const char* sprite = black ? "black" : "white";
    if (ctx.atlas && ctx.atlas->has(sprite))
    {
        // 图集里的棋子自身就带透明边与描边，直接画即可。
        // 不要在其下再垫一层自绘圆盘 —— 那会在棋子边缘露出一圈色差。
        // dest 必须传 img：离屏渲染时省略它会把精灵画到窗口上。
        ctx.atlas->draw(sprite, bv.spriteX(c, sz), bv.spriteY(c, sz), sz, img);
    }
    else
    {
        // 仅在图集缺图时才自绘，保证不会出现空洞
        setfillcolor(black ? theme::kSideFirst : theme::kSideSecond, img);
        fillellipse(bv.px(c), bv.py(c), static_cast<int>(sz * 0.46),
                    static_cast<int>(sz * 0.46), img);
        setcolor(theme::kBoardLine, img);
        circle(bv.px(c), bv.py(c), static_cast<int>(sz * 0.46), img);
    }
}

void FlipPuzzleGame::drawOverlay(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
{
    // 高阶才给的额外信息：把解法顺序直接标在棋盘上（1、2、3…）。
    // 低阶只靠推荐点 + HUD 文字，避免一上来就给答。
    if (!ctx.showHint || ctx.hintLevel < 4 || status() != GameStatus::Playing)
    {
        return;
    }

    const auto sol = solution();
    if (!sol.found || sol.press.empty())
    {
        return;
    }

    int order = 0;
    text::setFont(std::max(13, bv.cell() / 4), true, img);

    for (int i = 0; i < cols_ * rows_; ++i)
    {
        if (!sol.press[i])
        {
            continue;
        }
        ++order;

        const Coord c{i % cols_, i / cols_};

        // 小圆底 + 序号，放在格子偏上位置，不遮住棋子本身
        const int cx = bv.px(c);
        const int cy = bv.py(c) - static_cast<int>(bv.cell() * 0.30);
        const int r  = std::max(10, bv.cell() / 7);

        setfillcolor(theme::kAccent, img);
        fillellipse(cx, cy, r, r, img);
        setcolor(theme::kTextOnAcc, img);

        char num[8];
        std::snprintf(num, sizeof(num), "%d", order);
        text::drawCentered(num, cx, cy, img);

        // 必按格再套一个环，和 L5 的 HUD 说明对应
        if (ctx.hintLevel >= 5 && i < static_cast<int>(sol.required.size()) && sol.required[i])
        {
            setcolor(theme::kWarn, img);
            setlinewidth(3.0f, img);
            circle(cx, cy, r + 4, img);
            setlinewidth(1.0f, img);
        }
    }
}

Move FlipPuzzleGame::lastMove() const
{
    Move m{};
    if (!history_.empty())
    {
        m.to = history_.back();
    }
    return m;
}

} // namespace chess
