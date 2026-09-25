#pragma once

#include "games/game.h"
#include "ui/atlas.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  象棋（中国象棋）
// ---------------------------------------------------------------------------
//  棋盘固定 9 路 x 10 行（河界与九宫都依赖这个尺寸，不能改）。
//  坐标：x = 0..8（左→右），y = 0..9（上→下）。
//  先手（First）在下方（y = 7..9），后手（Second）在上方（y = 0..2）。
//
//  实现的规则：
//    * 马腿、象眼、炮的隔子、兵卒过河、士将不出九宫
//    * 飞将（双方将帅不得在同一直线上照面）
//    * 将军 / 将死 / 困毙（无着可走即负）
//    * 长将判负（同一局面第三次出现时，若反复将军的一方仍在将军则判负）
//    * 60 回合内无吃子判和
//
//  棋子的内部编码：0 = 空，其余 = type | (side << 3)，type 见 PieceType
// ---------------------------------------------------------------------------
namespace xq {

enum class PieceType : uint8_t
{
    None = 0,
    King,       // 帥 / 将
    Advisor,    // 仕 / 士
    Elephant,   // 相 / 象
    Horse,      // 马 / 馬
    Chariot,    // 车 / 車
    Cannon,     // 炮
    Pawn,       // 兵 / 卒
};

constexpr uint8_t kRed   = 0;   // 先手（下方）
constexpr uint8_t kBlack = 1;   // 后手（上方）

inline uint8_t makePiece(uint8_t side, PieceType t)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(t) | (side << 3));
}

inline PieceType typeOf(uint8_t p) { return static_cast<PieceType>(p & 0x07); }
inline uint8_t   sideOf(uint8_t p) { return (p >> 3) & 0x01; }
inline bool      isEmpty(uint8_t p) { return typeOf(p) == PieceType::None; }

constexpr int kCols = 9;
constexpr int kRows = 10;

inline int  idx(int x, int y) { return y * kCols + x; }
inline bool inBoard(int x, int y) { return x >= 0 && x < kCols && y >= 0 && y < kRows; }

// 棋子汉字（用于图集缺图时的文字兜底，以及 HUD 显示）
const char* pieceName(uint8_t piece);

// 该方是否在九宫内（士/将用）
bool inPalace(uint8_t side, int x, int y);

// 该方是否已过河（象不能过河、兵过河后可横走）
bool crossedRiver(uint8_t side, int y);

} // namespace xq

// ---------------------------------------------------------------------------
//  游戏类
// ---------------------------------------------------------------------------
class XiangqiGame : public IGame
{
public:
    GameDesc    desc() const override;
    std::string variantName() const override { return "标准象棋"; }

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
    int         moveCount() const override { return static_cast<int>(history_.size()) / 2; }

    bool hasPieceOf(Coord c, Side s) const override;
    bool selectableFrom(Coord c, Side s) const override;
    bool needsFromTo() const override { return true; }

    HintData basicHint(int level) const override;

    void drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const override;
    void drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const override;
    void drawOverlay(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const override;

    bool hasLastMove() const override { return !history_.empty(); }
    Move lastMove() const override;

    // ---- 供 AI 与自检使用 ----
    const uint8_t* cells() const { return board_; }
    uint8_t        at(int x, int y) const
    {
        return xq::inBoard(x, y) ? board_[xq::idx(x, y)] : 0;
    }

    // 该方是否正被将军
    bool inCheck(uint8_t side) const;

    // 某个棋子编码在 res/chesses.txt 里对应的精灵名；
    // 返回 nullptr 表示"该子没有图集素材"（例如红炮），绘制时会退到文字兜底。
    //
    // 单独暴露出来是为了能用 `chess --dump-board` 把整张映射表打到控制台核对：
    // 图集里的 shuai1/shi1/... 并未标明哪个是红哪个是黑，需要人工确认一次。
    static const char* spriteNameForPiece(uint8_t piece);

    // 非行棋方所有能"把行棋方将死/将军"的着法目的地。
    // 用于提示预警；放在游戏类里可以避免把内部棋盘数组暴露给 AI 层。
    std::vector<Coord> opponentCheckTargets() const;

    // 伪合法着法（不检查走后是否自己被将军）。perft 与搜索内部用。
    void genPseudoMoves(uint8_t side, std::vector<Move>& out) const;

    // 局面是否合法（双方将帅没有照面）
    bool kingsFacing() const;

private:
    struct MoveRec
    {
        Move    m;
        uint8_t captured = 0;
        uint64_t keyBefore = 0;
    };

    void     setStartPosition();
    void     updateStatusAfterMove();
    uint64_t keyOf(uint8_t side) const;
    bool     givesCheck(uint8_t side) const;

    GameConfig          cfg_{};
    uint8_t             board_[xq::kCols * xq::kRows]{};
    Side                sideToMove_ = Side::First;
    GameStatus          status_     = GameStatus::Playing;
    std::vector<MoveRec> history_;

    int noCapturePlies_ = 0;

    // 重复局面记录：每次落子后把自己的 key 记下来，用于长将/重复判定
    std::vector<uint64_t> keyHistory_;
};

} // namespace chess
