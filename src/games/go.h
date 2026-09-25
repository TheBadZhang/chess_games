#pragma once

#include "core/rng.h"
#include "games/game.h"
#include "ui/atlas.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  围棋
// ---------------------------------------------------------------------------
//  规则（中国规则 / 数子法）：
//    * 落子在交叉点；落子后先提掉对方无气的棋块
//    * 若自己提子后仍无气，则该着为**自杀**，非法
//    * **打劫**：简单劫 —— 不得立即提回上一手提掉的那一子
//      （不用全局同形反复，理由见下方说明）
//    * 停一手（pass）；双方连续停手则终局
//    * 终局按**数子法**计算：己方棋子数 + 己方围住的空点数
//      （中国规则不需要双方各自减去死子，判死由双方停手时的盘面决定）
//
//  为什么用简单劫而不是全局同形（superko）：
//    全局同形需要在每个局面节点持有历史局面的哈希集合。
//    本项目的 AI 会对局面做大量深拷贝（MCTS 每次模拟都要克隆/回滚），
//    带一个集合会让复制成本显著上升，而实战中简单劫已经能覆盖绝大多数争议，
//    因此这里按简单劫实现，并在文档与提示中明确说明。
//
//  棋子用 0/1/2 表示（空/黑/白）；黑先。
// ---------------------------------------------------------------------------
class GoGame : public IGame
{
public:
    GameDesc    desc() const override;
    std::string variantName() const override;

    void              setup(const GameConfig& cfg) override;
    const GameConfig& config() const override { return cfg_; }

    BoardSpec  boardSpec() const override;
    Side       sideToMove() const override { return sideToMove_; }
    GameStatus status() const override { return status_; }

    std::vector<Move> legalMoves() const override;
    bool              isLegal(const Move& m) const override;
    bool              apply(const Move& m) override;
    bool              undo() override;
    bool              canUndo() const override { return !history_.empty(); }

    uint64_t               stateKey() const override;
    std::unique_ptr<IGame> clone() const override;

    const char* sideName(Side s) const override;
    std::string statusText() const override;
    std::string scoreText() const override;
    int         moveCount() const override { return static_cast<int>(history_.size()); }

    const char* pieceSprite(Side s) const override
    {
        return s == Side::First ? "black" : (s == Side::Second ? "white" : nullptr);
    }

    bool hasPieceOf(Coord c, Side s) const override;
    bool needsPassButton() const override { return true; }

    HintData basicHint(int level) const override;

    void drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const override;
    void drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const override;
    void drawOverlay(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const override;

    bool hasLastMove() const override { return !history_.empty(); }
    Move lastMove() const override;

    // ---- 供 AI 与自检使用 ----
    int     cols() const { return cols_; }
    int     rows() const { return rows_; }
    uint8_t at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= cols_ || y >= rows_)
        {
            return kWall;
        }
        return cells_[static_cast<size_t>(y) * cols_ + x];
    }

    // 棋盘外视为"对方颜色"，这样数气时不必为边界写特例
    static constexpr uint8_t kWall = 3;

    // 该点是否是"打劫禁着点"（简单劫）
    bool isKoPoint(Coord c) const { return c.valid() && koPoint_ == c; }

    // 盘面当前是否存在打劫禁着点
    bool hasKoPoint() const { return koPoint_.valid(); }

    // 当前打劫禁着点（无则返回无效坐标）。
    // AI 需要把它同步进自己的精简局面，否则会选出游戏判定为非法的着法。
    Coord koPoint() const { return koPoint_; }

    // 该点是否是己方或对方的**眼**（用于 AI 避免自填眼、以及提示）
    // 判定：四邻全是本方棋子、且对角上本方棋子数 >= (边角 1 / 盘中 2)
    bool isEye(Coord c, uint8_t stone) const;

    // 该点的棋块有几口气；无子返回 -1
    int libertiesAt(Coord c) const;

    // 数子法计分。返回 {黑, 白(含贴目)}，单位是"子"
    struct Score
    {
        int black   = 0;
        int white   = 0;
        double komi = 0.0;
    };
    Score score() const;

    double komi() const { return komi_; }

    // 停手次数（连续）
    int consecutivePasses() const { return consecutivePasses_; }

private:
    struct MoveRec
    {
        Coord   at{};
        bool    pass     = false;
        int     captured = 0;
        Coord   koBefore{};
        // 被提掉的**每一枚**棋子的位置与颜色。
        // 不能只记录棋块起点 —— 一次提子会提掉整块，
        // 只存起点会让悔棋复原不回去。
        std::vector<Coord>   removedPos;
        std::vector<uint8_t> removedColor;
    };

    void   clearBoard();
    void   placeHandicap(int count);

    // 提掉 (x,y) 所在的整块；outRemoved 非空时收集被提每一子的位置
    int    removeDeadGroup(int x, int y, uint8_t stone, std::vector<Coord>* outRemoved = nullptr);

    int    countLiberties(int x, int y, uint8_t stone) const;
    bool   wouldBeSuicide(Coord c, uint8_t stone) const;
    void   finishWithScoring();

    GameConfig           cfg_{};
    int                  cols_ = 19;
    int                  rows_ = 19;
    std::vector<uint8_t> cells_;
    Side                 sideToMove_ = Side::First;
    GameStatus           status_     = GameStatus::Playing;

    Coord koPoint_{};
    double komi_ = 7.5;
    int    consecutivePasses_ = 0;

    std::vector<MoveRec> history_;
};

} // namespace chess
