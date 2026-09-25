#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  几何
// ---------------------------------------------------------------------------

struct Rect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    int  right() const { return x + w; }
    int  bottom() const { return y + h; }
    int  cx() const { return x + w / 2; }
    int  cy() const { return y + h / 2; }
    bool empty() const { return w <= 0 || h <= 0; }

    bool contains(int px, int py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    Rect inset(int d) const { return Rect{x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(int dx, int dy) const { return Rect{x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
};

// ---------------------------------------------------------------------------
//  对局双方
// ---------------------------------------------------------------------------
//  刻意用 First / Second 而不是 Black / White：
//    黑白棋 / 五子棋 / 围棋 是黑先，象棋是红先；
//    具体显示名与棋子样式由各游戏自己给出（IGame::sideName / drawCell）。
enum class Side : uint8_t
{
    None   = 0,
    First  = 1,
    Second = 2,
};

constexpr Side otherSide(Side s)
{
    return s == Side::First ? Side::Second : (s == Side::Second ? Side::First : Side::None);
}

// ---------------------------------------------------------------------------
//  坐标
// ---------------------------------------------------------------------------
//  两种语义共用同一个结构，由 BoardSpec::kind 决定几何换算：
//    - 落格类（黑白棋 / 五子棋）：一个格子
//    - 交叉点类（围棋 / 象棋）：一个交叉点
struct Coord
{
    int x = -1;
    int y = -1;

    constexpr bool valid() const { return x >= 0 && y >= 0; }
};

constexpr bool operator==(Coord a, Coord b) { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(Coord a, Coord b) { return !(a == b); }

// ---------------------------------------------------------------------------
//  着法
// ---------------------------------------------------------------------------
struct Move
{
    Coord from{};   // 仅需要"起点"的游戏（象棋）使用；其余游戏留空
    Coord to{};     // pass 用 to = (-1,-1)（即 !to.valid()）表示

    constexpr bool isPass() const { return !to.valid(); }
};

constexpr bool operator==(Move a, Move b) { return a.from == b.from && a.to == b.to; }
constexpr bool operator!=(Move a, Move b) { return !(a == b); }

// ---------------------------------------------------------------------------
//  对局状态
// ---------------------------------------------------------------------------

enum class GameStatus : uint8_t
{
    Playing = 0,
    FirstWin,
    SecondWin,
    Draw,
};

// ---------------------------------------------------------------------------
//  提示系统
// ---------------------------------------------------------------------------
//  设计目标：同一套结构承载 5 个等级的提示，等级越高填的字段越多；
//  渲染层只看"哪些字段有值"决定画什么，不需要知道当前等级。
//
//    L1  只填 cells（Legal 圆点 / Illegal 灰叉）
//    L2  追加 Danger / Threat 类 cells
//    L3  追加 lines（推荐着法）+ notes
//    L4  追加多条 lines（主变）+ depth
//    L5  追加 hasWinRate / hasEval
// ---------------------------------------------------------------------------

enum class HintKind : uint8_t
{
    Legal = 0,      // 合法着点（小圆点）
    Illegal,        // 非法 / 禁着（灰叉）
    Good,           // 有利位置（绿）
    Danger,         // 危险位置：对手能在此占到便宜（橙）
    Threat,         // 对手的威胁点（红）
    Recommended,    // 推荐着法（高亮圈）
};

struct HintCell
{
    Coord       coord{};
    HintKind    kind   = HintKind::Legal;
    float       weight = 0.0f;   // 0..1，渲染成透明度/线宽
    std::string note;            // 可选短标签
};

// 一条变化（主变 / 对手最佳应手）
struct HintLine
{
    std::vector<Move> moves;
    int               score = 0;   // 从"当前行棋方"视角的评估分
    std::string       label;
};

struct HintData
{
    std::vector<HintCell>    cells;
    std::vector<HintLine>    lines;
    std::vector<std::string> notes;    // 文字结论，按行显示（每行一个字符串）

    bool        hasEval = false;
    int         evalCp  = 0;           // 厘兵（centipawn）为单位的评估分
    std::string evalText;              // 已格式化好的文本，如 "+1.35"

    bool   hasWinRate = false;
    double winRate    = 0.5;           // 当前行棋方胜率 0..1

    int       depth      = 0;
    long long nodes      = 0;
    int       thinkingMs = 0;
    bool      complete   = false;      // false 表示搜索被取消/超时，结果是部分的

    // ---- MCTS 专用诊断 ----
    // 平均每局模拟的手数。它能直接反映 playout 是否被意外截断：
    // 9 路一局完整模拟大约 80~160 手，若这个值只有十几，说明
    // 模拟几乎没走几步就退出了（通常是计时基准写错），MCTS 已经退化成随机。
    int mctsPlayouts      = 0;
    int mctsAvgPlayoutLen = 0;
};

// ---------------------------------------------------------------------------
//  棋盘规格
// ---------------------------------------------------------------------------

enum class BoardKind : uint8_t
{
    CellGrid = 0,      // 落格：黑白棋 / 五子棋
    Intersections,     // 交叉点：围棋 / 象棋
};

struct BoardSpec
{
    BoardKind kind = BoardKind::Intersections;
    int       cols = 8;
    int       rows = 8;
};

} // namespace chess
