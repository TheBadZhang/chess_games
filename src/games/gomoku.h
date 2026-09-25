#pragma once

#include "core/rng.h"
#include "games/game.h"
#include "games/gomoku_eval.h"
#include "ui/atlas.h"

#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  五子棋
// ---------------------------------------------------------------------------
//  规则：
//    * 自由规则：任意一方先连成 5 子及以上即胜（长连也算胜）
//    * 黑棋禁手（可选）：长连、双四、双活三，黑下到这些点即判负
//      —— 具体判定见 games/gomoku_eval.h 里 ForbiddenInfo 的说明
//
//  棋盘：9x9 / 13x13 / 15x15（默认，标准）/ 19x19
//  棋子落在**交叉点**上（围棋/象棋同款），所以 BoardKind = Intersections
// ---------------------------------------------------------------------------
class GomokuGame : public IGame
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

    HintData basicHint(int level) const override;

    void drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const override;
    void drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const override;

    bool hasLastMove() const override { return !history_.empty(); }
    Move lastMove() const override;

    // ---- 供 AI 与自检使用 ----
    const gomoku::Board& board() const { return board_; }
    bool  ruleForbidden() const { return cfg_.variant == 1; }
    int   lastX() const { return history_.empty() ? -1 : history_.back().x; }
    int   lastY() const { return history_.empty() ? -1 : history_.back().y; }

    // 候选点：已有棋子附近 2 格内的空点（空盘时给中心）
    std::vector<Coord> candidateMoves() const;

private:
    uint8_t sideValue(Side s) const { return s == Side::First ? 1 : 2; }

    void    updateStatusAfterMove(Coord c);
    bool    moveIsForbidden(Coord c) const;

    GameConfig        cfg_{};
    gomoku::Board     board_{};
    Side              sideToMove_ = Side::First;
    GameStatus        status_     = GameStatus::Playing;
    std::vector<Coord> history_;
};

} // namespace chess
