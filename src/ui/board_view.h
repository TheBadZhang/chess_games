#pragma once

#include "core/types.h"

#include <graphics.h>

namespace chess {

// ---------------------------------------------------------------------------
//  通用棋盘几何 + 渲染
// ---------------------------------------------------------------------------
//  两种棋盘模型共用同一套换算，这是刻意的：
//
//    CellGrid（落格：黑白棋 / 五子棋）
//        boardRect = cols*cell × rows*cell，格子铺满整个矩形
//        第 (gx,gy) 格中心 = boardRect.x + gx*cell + cell/2
//
//    Intersections（交叉点：围棋 / 象棋）
//        boardRect = cols*cell × rows*cell，线在四边各留 cell/2 的木边
//        第 (gx,gy) 个交叉点 = boardRect.x + cell/2 + gx*cell
//
//    因为 cell/2 + (cols-1)*cell + cell/2 == cols*cell，
//    两种模型的 boardRect 尺寸公式完全一致：
//        cell = min(availW / cols, availH / rows)
//    于是"布局 / 命中测试 / 提示叠加"的代码只需写一份。
// ---------------------------------------------------------------------------
class BoardView
{
public:
    void layout(const BoardSpec& spec, const Rect& area);

    const BoardSpec& spec() const { return spec_; }

    // 棋盘实际占用的像素矩形（不含坐标标注）
    const Rect& boardRect() const { return board_; }

    // 格边长
    int cell() const { return cell_; }
    // 交叉点模式下从 boardRect 边缘到最外侧线的距离
    int margin() const { return margin_; }

    // ---- 坐标 <-> 像素 ----

    // 坐标对应的中心像素位置
    int px(Coord c) const;
    int py(Coord c) const;

    // 以该坐标为中心、边长为 size 的方格左上角
    int spriteX(Coord c, int size) const { return px(c) - size / 2; }
    int spriteY(Coord c, int size) const { return py(c) - size / 2; }

    // 建议的棋子边长
    int pieceSize(double factor = 1.0) const;

    // 像素 -> 坐标。太远（超过 cell 的 0.6 倍）返回无效坐标。
    Coord hitTest(int mx, int my) const;

    // 坐标是否在盘内
    bool inBounds(Coord c) const;

    // 按行列顺序遍历（供游戏绘制棋盘时使用）
    int cols() const { return spec_.cols; }
    int rows() const { return spec_.rows; }

    // ---- 绘制 ----

    // 木色底 + 外框 + 投影
    void drawBoardBase(PIMAGE img) const;

    // 棋盘线（CellGrid 画格子线，Intersections 画交叉线网）
    void drawGrid(PIMAGE img) const;

    // 通用坐标标注：列用字母（跳过 I），行用数字
    void drawAlphaNumLabels(PIMAGE img) const;

    // ---- 叠加层 ----

    void drawLastMove(PIMAGE img, Coord c, color_t c1, color_t c2) const;
    void drawSelection(PIMAGE img, Coord c, color_t col) const;
    void drawHover(PIMAGE img, Coord c, color_t col) const;
    void drawHint(PIMAGE img, Coord c, HintKind kind, float weight) const;

private:
    BoardSpec spec_{};
    Rect      board_{};
    int       cell_   = 0;
    int       margin_ = 0;
};

} // namespace chess
