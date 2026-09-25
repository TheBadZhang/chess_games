#include "ui/board_view.h"

#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chess {

void BoardView::layout(const BoardSpec& spec, const Rect& area)
{
    spec_ = spec;

    const int cols = std::max(1, spec.cols);
    const int rows = std::max(1, spec.rows);

    // 留一点边距给坐标标注 / 棋子溢出的部分
    const int pad = 24;
    const int availW = std::max(16, area.w - 2 * pad);
    const int availH = std::max(16, area.h - 2 * pad);

    cell_ = std::max(8, std::min(availW / cols, availH / rows));

    // 两种模型尺寸公式一致，见头文件说明
    const int bw = cell_ * cols;
    const int bh = cell_ * rows;

    board_ = Rect{area.x + (area.w - bw) / 2, area.y + (area.h - bh) / 2, bw, bh};
    margin_ = (spec_.kind == BoardKind::Intersections) ? cell_ / 2 : 0;

    // 交叉点模式下，第一条线距 board_ 左边缘 margin_（而非 0），
    // 因此把 board_ 往左上挪 margin_，让"线网"在视觉上落在区域正中
    if (spec_.kind == BoardKind::Intersections)
    {
        board_.x -= margin_;
        board_.y -= margin_;
    }
}

int BoardView::px(Coord c) const
{
    if (spec_.kind == BoardKind::CellGrid)
    {
        return board_.x + c.x * cell_ + cell_ / 2;
    }
    return board_.x + margin_ + c.x * cell_;
}

int BoardView::py(Coord c) const
{
    if (spec_.kind == BoardKind::CellGrid)
    {
        return board_.y + c.y * cell_ + cell_ / 2;
    }
    return board_.y + margin_ + c.y * cell_;
}

int BoardView::pieceSize(double factor) const
{
    return std::max(4, static_cast<int>(std::lround(cell_ * factor)));
}

bool BoardView::inBounds(Coord c) const
{
    return c.x >= 0 && c.y >= 0 && c.x < spec_.cols && c.y < spec_.rows;
}

Coord BoardView::hitTest(int mx, int my) const
{
    if (cell_ <= 0)
    {
        return Coord{};
    }

    if (spec_.kind == BoardKind::CellGrid)
    {
        // 落格类：格子在 [board_] 里**平铺**，所以直接取 floor 得到格子索引。
        //
        // 这里曾经用 lround（那是交叉点该用的"取最近点"），结果在 cell_ 为偶数时
        // cell_/2 恰好等于 0.5，lround(c + 0.5) 一律 +1 —— 8x8 棋盘上每一点都
        // 偏一格；奇数格时偏移 0.496 < 0.5 才侥幸没暴露，但边界附近仍会错。
        // 判定标准：点必须落在棋盘矩形内（严格平铺，不做外扩，避免误触发落子）。
        const double gx = static_cast<double>(mx - board_.x) / cell_;
        const double gy = static_cast<double>(my - board_.y) / cell_;

        const Coord c{static_cast<int>(std::floor(gx)), static_cast<int>(std::floor(gy))};
        return inBounds(c) ? c : Coord{};
    }

    // 交叉点类：取最近的交叉点，并要求距离在半个格以内。
    // 注意这里要减掉 margin_：px(c) = board_.x + margin_ + c.x * cell_
    // （layout 里已把 board_ 往左上挪过 margin_，使线网在区域内居中）。
    const double gx = static_cast<double>(mx - board_.x - margin_) / cell_;
    const double gy = static_cast<double>(my - board_.y - margin_) / cell_;

    const Coord c{static_cast<int>(std::lround(gx)), static_cast<int>(std::lround(gy))};
    if (!inBounds(c))
    {
        return Coord{};
    }
    if (std::fabs(gx - c.x) > 0.5 || std::fabs(gy - c.y) > 0.5)
    {
        return Coord{};   // 离最近的交叉点太远 -> 不算点中
    }
    return c;
}

// ---------------------------------------------------------------------------
//  绘制
// ---------------------------------------------------------------------------

void BoardView::drawBoardBase(PIMAGE img) const
{
    // 投影：几层外扩的深色描边（比真投影便宜，观感差别很小）
    for (int i = 6; i >= 1; --i)
    {
        const Rect sr{board_.x - i, board_.y + i, board_.w + 2 * i, board_.h + 2 * i};
        ui::strokeRound(img, sr, std::max(2, theme::kRadius + i), theme::kBoardShadow, 1);
    }

    ui::fillBox(img, board_, theme::kBoardBg);

    // 外框内缩半个线宽：GDI 的描边是以路径为中心向两侧各画 W/2 的，
    // 若直接沿 board_ 描边，会向外溢出 1px，使"视觉棋盘"比"逻辑棋盘"大一圈。
    // 内缩 1px + 线宽 2 之后，描边恰好覆盖 board_ 的边界像素，
    // 视觉范围与 hitTest 的范围严格重合。
    ui::strokeBox(img, board_.inset(1), theme::kBoardEdge, 2);
}

void BoardView::drawGrid(PIMAGE img) const
{
    setcolor(theme::kBoardLine, img);
    setlinewidth(1.0f, img);

    if (spec_.kind == BoardKind::CellGrid)
    {
        for (int x = 0; x <= spec_.cols; ++x)
        {
            const int gx = board_.x + x * cell_;
            line(gx, board_.y, gx, board_.bottom() - 1, img);
        }
        for (int y = 0; y <= spec_.rows; ++y)
        {
            const int gy = board_.y + y * cell_;
            line(board_.x, gy, board_.right() - 1, gy, img);
        }
    }
    else
    {
        for (int x = 0; x < spec_.cols; ++x)
        {
            const int gx = px(Coord{x, 0});
            line(gx, py(Coord{0, 0}), gx, py(Coord{0, spec_.rows - 1}), img);
        }
        for (int y = 0; y < spec_.rows; ++y)
        {
            const int gy = py(Coord{0, y});
            line(px(Coord{0, 0}), gy, px(Coord{spec_.cols - 1, 0}), gy, img);
        }
    }
    setlinewidth(1.0f, img);
}

void BoardView::drawAlphaNumLabels(PIMAGE img) const
{
    text::setFont(std::clamp(cell_ / 3, 12, 20), false, img);
    setcolor(theme::kTextFaint, img);

    char buf[8];

    // 列：字母，跳过 I（围棋惯例）
    for (int x = 0; x < spec_.cols; ++x)
    {
        int idx = x;
        if (idx >= 8)
        {
            ++idx;   // 跳过 'I'
        }
        std::snprintf(buf, sizeof(buf), "%c", static_cast<char>('A' + idx));
        text::drawCentered(buf, px(Coord{x, 0}) + 0, board_.y - cell_ / 2, img);
    }

    // 行：数字
    for (int y = 0; y < spec_.rows; ++y)
    {
        std::snprintf(buf, sizeof(buf), "%d", y + 1);
        text::drawCentered(buf, board_.x - cell_ / 2, py(Coord{0, y}), img);
    }
}

// ---------------------------------------------------------------------------
//  叠加层
// ---------------------------------------------------------------------------

void BoardView::drawLastMove(PIMAGE img, Coord c, color_t c1, color_t c2) const
{
    if (!inBounds(c))
    {
        return;
    }
    const int cx = px(c);
    const int cy = py(c);
    const int r  = std::max(3, static_cast<int>(cell_ * 0.16));

    // 双色小方块：在深浅两种棋子底色上都能看清
    ui::fillRound(img, Rect{cx - r, cy - r, 2 * r, 2 * r}, 2, c1);
    ui::fillRound(img, Rect{cx - r + 2, cy - r + 2, 2 * r - 4, 2 * r - 4}, 1, c2);
}

void BoardView::drawSelection(PIMAGE img, Coord c, color_t col) const
{
    if (!inBounds(c))
    {
        return;
    }
    const int cx = px(c);
    const int cy = py(c);
    const int r  = std::max(6, static_cast<int>(cell_ * 0.46));

    setcolor(col, img);
    setlinewidth(3.0f, img);
    if (spec_.kind == BoardKind::CellGrid)
    {
        rectangle(cx - r, cy - r, cx + r, cy + r, img);
    }
    else
    {
        circle(cx, cy, r, img);
    }
    setlinewidth(1.0f, img);
}

void BoardView::drawHover(PIMAGE img, Coord c, color_t col) const
{
    if (!inBounds(c))
    {
        return;
    }
    const int cx = px(c);
    const int cy = py(c);
    const int r  = std::max(4, static_cast<int>(cell_ * 0.34));

    setcolor(col, img);
    setlinewidth(2.0f, img);
    if (spec_.kind == BoardKind::CellGrid)
    {
        rectangle(cx - r, cy - r, cx + r, cy + r, img);
    }
    else
    {
        circle(cx, cy, r, img);
    }
    setlinewidth(1.0f, img);
}

void BoardView::drawHint(PIMAGE img, Coord c, HintKind kind, float weight) const
{
    if (!inBounds(c))
    {
        return;
    }
    ui::hintMarker(img, px(c), py(c), std::max(5, static_cast<int>(cell_ * 0.46)),
                   kind, weight);
}

} // namespace chess
