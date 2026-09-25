#pragma once

#include "core/types.h"
#include "ui/board_view.h"

#include <graphics.h>

#include <memory>
#include <string>
#include <vector>

namespace chess {

// 前向声明（GameDesc 只需要一个函数指针类型的返回类型）
class IGame;
class IAI;
class Atlas;

// ---------------------------------------------------------------------------
//  游戏标识与配置
// ---------------------------------------------------------------------------

// 棋盘预设（象棋是 9x10，其余三棋正方形）
struct BoardPreset
{
    const char* label = "";
    int         cols  = 8;
    int         rows  = 8;
};

// AI 等级描述
struct LevelDesc
{
    const char* name   = "";   // 短名，如 "入门"
    const char* detail = "";   // 提示层级说明，如 "显示所有可走点"
};

// 菜单里可调的配置
struct GameConfig
{
    int  variant   = 0;   // 变体索引
    int  cols      = 8;
    int  rows      = 8;
    int  aiLevel   = 3;   // 1..5
    int  humanSide = 1;   // 1 = 人执 First，2 = 人执 Second
    int  handicap  = 0;   // 围棋让子数
    bool ruleOption = false;   // 例如五子棋的禁手
    bool humanVsHuman = false;
    uint64_t seed = 20240925ull;
};

// ---------------------------------------------------------------------------
//  游戏描述符（注册表用，静态信息）
// ---------------------------------------------------------------------------
struct GameDesc
{
    const char* id    = "";
    const char* name  = "";
    const char* blurb = "";

    std::vector<std::string>     variants;      // 空 = 无变体可选
    std::vector<BoardPreset>     boardPresets;  // 棋盘尺寸候选
    std::vector<LevelDesc>       levels;        // 5 个等级
    const char*                  ruleOptionName = nullptr;  // 非空则显示一个开关
    const char*                  handicapName   = nullptr;  // 非空则显示让子选择

    // 工厂：创建该游戏的实例（未 setup）
    std::unique_ptr<IGame> (*create)() = nullptr;
    // 工厂：创建该游戏对应的 AI 引擎（可空 → 该游戏无搜索型 AI）
    std::unique_ptr<IAI> (*createAi)() = nullptr;
};

// ---------------------------------------------------------------------------
//  渲染上下文
// ---------------------------------------------------------------------------
//  把绘制时需要的外部信息集中传递，避免游戏反向依赖 App：
//    * atlas    —— 棋子图集
//    * hintLevel/showHint —— 当前提示等级，游戏可以据此"高阶多给信息"
//      （例如低阶只标可走点，高阶直接把解法顺序写到棋盘上）
//    * hover/selected —— 交互态
struct DrawContext
{
    const Atlas* atlas      = nullptr;
    int          hintLevel  = 0;      // 0 表示不显示提示
    bool         showHint   = false;
    Coord        hover{};
    Coord        selected{};
};

// ---------------------------------------------------------------------------
//  游戏接口
// ---------------------------------------------------------------------------
//  约定：
//    * 逻辑方法（legalMoves / apply / undo / clone / stateKey / basicHint）
//      必须是**纯计算**，不得触碰任何 EGE 绘图函数 —— 因为 AI 线程会在
//      自己的 clone 上调用它们。
//    * draw* 方法只在主线程调用。
// ---------------------------------------------------------------------------
class IGame
{
public:
    virtual ~IGame() = default;

    // ---- 元信息 ----
    virtual GameDesc    desc() const = 0;
    virtual std::string variantName() const = 0;

    // ---- 生命周期 ----
    // 按配置重置到初始局面
    virtual void setup(const GameConfig& cfg) = 0;
    virtual const GameConfig& config() const = 0;

    // ---- 棋盘与状态 ----
    virtual BoardSpec  boardSpec() const = 0;
    virtual Side       sideToMove() const = 0;
    virtual GameStatus status() const = 0;
    virtual bool       isOver() const { return status() != GameStatus::Playing; }
    // 本游戏是否有"双方"概念。单人解谜类（如翻转棋）返回 false，
    // HUD 会改用"单人对局"的呈现方式，也不会自动让 AI 代走。
    virtual bool hasSides() const { return true; }
    // ---- 规则 ----
    virtual std::vector<Move> legalMoves() const = 0;
    virtual bool              isLegal(const Move& m) const = 0;
    virtual bool              apply(const Move& m) = 0;
    virtual bool              undo() = 0;
    virtual bool              canUndo() const = 0;
    virtual void              pass() {}     // 仅围棋强制 pass 模式需要

    // ---- 局面指纹（重复局面 / 和棋判定）----
    virtual uint64_t stateKey() const = 0;

    // ---- 深拷贝（AI 在独立线程搜索，必须与 UI 局面隔离）----
    virtual std::unique_ptr<IGame> clone() const = 0;

    // ---- 文本 ----
    virtual const char* sideName(Side s) const = 0;
    virtual std::string statusText() const = 0;
    virtual std::string scoreText() const { return {}; }

    // 已走的着数（悔棋 / HUD 显示用）
    virtual int moveCount() const = 0;

    // 该方的棋子在图集里的精灵名；返回 nullptr 表示不使用图集
    // （HUD 会改画一个纯色圆盘，象棋就是这种情况）
    virtual const char* pieceSprite(Side s) const
    {
        return s == Side::First ? "black" : (s == Side::Second ? "white" : nullptr);
    }

    // 是否需要 UI 上的"停一手 / Pass"按钮（围棋需要）
    virtual bool needsPassButton() const { return false; }

    // 该等级下游戏自身就能给出的提示（不需要搜索）：
    //   L1 -> Legal / Illegal
    //   L2 -> 追加 Danger / Threat 等静态分析
    // 更高级别的（推荐着法 / 主变 / 胜率）由 AI 搜索填充。
    virtual HintData basicHint(int level) const { (void)level; return {}; }

    // ---- 渲染（仅主线程）----
    // 棋盘底上的装饰：星位、河界、九宫、坐标标注等
    virtual void drawDecorations(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
    {
        (void)bv;
        (void)ctx;
        (void)img;
    }
    // 某个坐标上的棋子
    virtual void drawCell(const BoardView& bv, Coord c, const DrawContext& ctx, PIMAGE img) const
    {
        (void)bv;
        (void)c;
        (void)ctx;
        (void)img;
    }
    // 棋盘之上的附加标记（最后一手除外，那由 BoardView 统一画）。
    // 适合放"高阶才给出的额外信息"，例如解法顺序编号。
    virtual void drawOverlay(const BoardView& bv, const DrawContext& ctx, PIMAGE img) const
    {
        (void)bv;
        (void)ctx;
        (void)img;
    }

    // 是否需要"点起点再点终点"的走子方式（象棋需要）
    virtual bool needsFromTo() const { return false; }

    // 该坐标上是否有 side 方的棋子（needsFromTo 的游戏用来判断可选中）
    virtual bool hasPieceOf(Coord c, Side s) const
    {
        (void)c;
        (void)s;
        return false;
    }

    // 轮到 side 走时，该坐标是否是一个"可以选中的起点"
    virtual bool selectableFrom(Coord c, Side s) const { return hasPieceOf(c, s); }

    // 最后一手（用于高亮）
    virtual bool hasLastMove() const = 0;
    virtual Move lastMove() const    = 0;
};

// ---------------------------------------------------------------------------
//  提示合并：把游戏自带的低级提示与 AI 搜索出的高级提示合成一份
// ---------------------------------------------------------------------------
inline void mergeHint(HintData& into, const HintData& from)
{
    into.cells.insert(into.cells.end(), from.cells.begin(), from.cells.end());
    into.lines.insert(into.lines.end(), from.lines.begin(), from.lines.end());
    into.notes.insert(into.notes.end(), from.notes.begin(), from.notes.end());

    if (from.hasEval)
    {
        into.hasEval  = true;
        into.evalCp   = from.evalCp;
        into.evalText = from.evalText;
    }
    if (from.hasWinRate)
    {
        into.hasWinRate = true;
        into.winRate    = from.winRate;
    }
    if (from.depth > into.depth)
    {
        into.depth = from.depth;
    }
    if (from.nodes > into.nodes)
    {
        into.nodes = from.nodes;
    }
    into.thinkingMs += from.thinkingMs;
    into.complete = into.complete || from.complete;
}

} // namespace chess
