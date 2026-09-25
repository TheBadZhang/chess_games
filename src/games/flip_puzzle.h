#pragma once

#include "core/rng.h"
#include "games/game.h"
#include "ui/atlas.h"

#include <cstdint>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  翻转棋（黑白棋的 flip 变体）
// ---------------------------------------------------------------------------
//  规则：
//    * 棋盘每个格子必有一颗棋子（黑或白），没有空点
//    * 点击一格：把 **该格 + 上下左右** 一起翻色（越界的不翻）
//    * 目标：把全盘翻成同一种颜色（全黑或全白都算完成）
//    * 不分先后手，也不分轮次 —— 这是单人解谜，计步数
//
//  为什么能精确求解：
//    每次按键是"翻 5 个格子"，而翻色等价于模 2 加法，所以
//      "每个格子最终要不要被翻" 与 "点了哪些格子" 之间是 GF(2) 上的线性关系。
//    于是可以解线性方程组，直接得到精确解、最少步数与"所有解都必须点的格子"。
//    这就是 ai/gf2.h 的用途。
//
//  生成可解局面的方式：
//    从全白出发，随机按 N 次。因为按键是自逆的（按两次等于没按），
//    所以把同样的 N 次再按一遍必定回到全白 —— 局面一定可解。
// ---------------------------------------------------------------------------

namespace flip {

// 棋子的内部表示：0 = 先手方（黑），1 = 后手方（白）
using Cell = uint8_t;
constexpr Cell kBlack = 0;
constexpr Cell kWhite = 1;

inline int indexOf(int x, int y, int cols) { return y * cols + x; }

// 求出把 board 全部翻成 target 颜色的一个方案。
// board 长度必须是 cols*rows。
struct Solution
{
    bool                 found      = false;
    std::vector<uint8_t> press;       // 长度 cols*rows，1 表示需要点这一格
    int                  pressCount = 0;
    int                  nullity    = 0;   // 自由变量个数（解的空间维数）
    std::vector<uint8_t> required;    // 所有解都必须点的格子（能确定下来的那部分）
};

// 指定目标颜色求解；优先返回"按键最少"的那个解
Solution solveTo(const std::vector<Cell>& board, int cols, int rows, Cell target);

// 在"全黑"与"全白"两个目标之间取按键更少的方案（平局偏向全白）
Solution solveAny(const std::vector<Cell>& board, int cols, int rows);

// 就地执行一次按键
void applyPress(std::vector<Cell>& board, int cols, int rows, int x, int y);

// 统计两种颜色的数量，返回 {黑数, 白数}
std::pair<int, int> countColors(const std::vector<Cell>& board);

// "离全同色还差多少"的势：min(黑数, 白数)，0 表示已完成
inline int potential(const std::vector<Cell>& board)
{
    const auto c = countColors(board);
    return c.first < c.second ? c.first : c.second;
}

} // namespace flip

// ---------------------------------------------------------------------------
//  游戏类
// ---------------------------------------------------------------------------
class FlipPuzzleGame : public IGame
{
public:
    GameDesc    desc() const override;
    std::string variantName() const override { return variantName_; }

    void              setup(const GameConfig& cfg) override;
    const GameConfig& config() const override { return cfg_; }

    BoardSpec  boardSpec() const override;
    Side       sideToMove() const override { return Side::First; }
    GameStatus status() const override;
    bool       hasSides() const override { return false; }

    std::vector<Move> legalMoves() const override;
    bool              isLegal(const Move& m) const override;
    bool              apply(const Move& m) override;
    bool              undo() override;
    bool              canUndo() const override { return !history_.empty(); }

    uint64_t                       stateKey() const override;
    std::unique_ptr<IGame>         clone() const override;

    const char* sideName(Side s) const override;
    std::string statusText() const override;
    std::string scoreText() const override;
    int         moveCount() const override { return static_cast<int>(history_.size()); }

    // 不使用图集以外的文字棋子，颜色由 pieceSprite 决定
    const char* pieceSprite(Side s) const override
    {
        return s == Side::First ? "black" : (s == Side::Second ? "white" : nullptr);
    }

    HintData basicHint(int level) const override;

    void drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const override;
    void drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const override;
    void drawOverlay(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const override;

    bool hasLastMove() const override { return !history_.empty(); }
    Move lastMove() const override;

    // ---- 供 AI 使用 ----
    const std::vector<flip::Cell>& cells() const { return cells_; }
    int  cols() const { return cols_; }
    int  rows() const { return rows_; }
    // 缓存的精确解（按局面指纹失效）
    const flip::Solution& solution() const;

private:
    void scramble();

    GameConfig           cfg_{};
    std::string          variantName_;
    int                  cols_ = 5;
    int                  rows_ = 5;
    std::vector<flip::Cell> cells_;
    std::vector<Coord>   history_;
    Rng                  rng_;

    mutable uint64_t       solveKey_ = ~0ull;
    mutable flip::Solution solveCache_{};
};

} // namespace chess
