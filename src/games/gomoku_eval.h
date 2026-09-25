#pragma once

#include "core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chess {
namespace gomoku {

// ---------------------------------------------------------------------------
//  棋盘
// ---------------------------------------------------------------------------
//  0 = 空，1 = 先手（黑），2 = 后手（白）
struct Board
{
    int                  cols = 15;
    int                  rows = 15;
    std::vector<uint8_t> cells;

    void reset(int c, int r);

    uint8_t at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= cols || y >= rows)
        {
            return kWall;
        }
        return cells[static_cast<size_t>(y) * cols + x];
    }

    void set(int x, int y, uint8_t v) { cells[static_cast<size_t>(y) * cols + x] = v; }

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < cols && y < rows; }

    int size() const { return cols * rows; }
    bool empty(int x, int y) const { return at(x, y) == 0; }

    // 棋盘外：视为"被堵住"，这样线型匹配不用为边界写特例
    static constexpr uint8_t kWall = 3;
};

// ---------------------------------------------------------------------------
//  棋型
// ---------------------------------------------------------------------------
enum class Shape : uint8_t
{
    None = 0,
    One,
    Two,          // 眠二
    OpenTwo,      // 活二
    Three,        // 眠三
    OpenThree,    // 活三（下一步可成活四）
    Four,         // 冲四（下一步可成五）
    OpenFour,     // 活四（有两个成五点）
    Five,         // 成五
    Overline,     // 长连（>= 6）
};

const char* shapeName(Shape s);

// 棋型分值。同一档位之间的差别对这个游戏不重要，
// 关键是"成五/活四/冲四/活三"之间的量级要拉开。
int shapeScore(Shape s);

// 4 个方向：(1,0) 横、(0,1) 竖、(1,1) 主对角、(1,-1) 反对角
constexpr int kDirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

// 假设在 (x,y) 落一枚 side 的棋子后，该方向上的棋型。
// 要求 (x,y) 当前为空（调用方保证）。
Shape shapeAt(const Board& b, int x, int y, int side, int dir);

// 4 个方向中最好的棋型
Shape bestShape(const Board& b, int x, int y, int side);

// 落子后该点的"进攻价值"（把 4 个方向的分值相加，但成五直接返回极大值）
int pointScore(const Board& b, int x, int y, int side);

// 落子后是否会形成五连或更长（自由规则下的取胜判定）
bool makesFiveOrMore(const Board& b, int x, int y, int side);

// 落子后是否形成**恰好**五连。
//
// 必须与 makesFiveOrMore 分开：禁手判定里"成五优先于禁手"用的是这个。
// 若错用 >=5，长连（>=6）会被当成"成五"而放行 —— 长连禁手就永远检不出来。
bool makesExactlyFive(const Board& b, int x, int y, int side);

// (x,y) 处沿某方向是否已连成 >= need（含 (x,y)）
int runLength(const Board& b, int x, int y, int side, int dir);

// ---------------------------------------------------------------------------
//  胜负
// ---------------------------------------------------------------------------

// 自由规则：>= 5 即胜（含长连）
bool isWinAt(const Board& b, int x, int y, int side);

// ---------------------------------------------------------------------------
//  禁手（黑棋）
// ---------------------------------------------------------------------------
//  说明：这是对连珠禁手规则的**工程化近似**，并非严格连珠：
//    * 长连      —— 落子后某方向连成 >= 6，判禁手（成五优先，不判禁手）
//    * 双四      —— 4 个方向里有 >= 2 个方向形成"冲四"或"活四"
//    * 双活三    —— 4 个方向里有 >= 2 个方向形成"活三"
//  严格连珠还要区分"假活三"（可被对方挡住而不成活四）等情形，
//  这里按方向计数，实战手感与连珠禁手基本一致。
struct ForbiddenInfo
{
    bool  forbidden = false;
    bool  overline  = false;
    int   fourCount = 0;       // 形成冲四/活四的方向数
    int   openThreeCount = 0;  // 形成活三的方向数
};

ForbiddenInfo checkForbidden(const Board& b, int x, int y);

} // namespace gomoku
} // namespace chess
