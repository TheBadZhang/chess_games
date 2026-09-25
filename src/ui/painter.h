#pragma once

#include "core/types.h"
#include "ui/theme.h"

#include <graphics.h>

namespace chess {
namespace ui {

// ---------------------------------------------------------------------------
//  图元层
// ---------------------------------------------------------------------------
//  所有函数都显式接收目标 PIMAGE（默认 NULL = 窗口）。
//
//  注意：这些图元内部走的是 GDI（bar / roundrect / line / 文字），**不会写像素的
//  alpha 字节**。若要把画好的内容存成 PNG 或再合成到别处，必须先用
//  chess::makeImageOpaque() 补齐 alpha —— 详见 ui/gfx_util.h。
// ---------------------------------------------------------------------------

void fillBox(PIMAGE img, const Rect& r, color_t fill);
void strokeBox(PIMAGE img, const Rect& r, color_t line, int thickness = 1);

void fillRound(PIMAGE img, const Rect& r, int radius, color_t fill);
void strokeRound(PIMAGE img, const Rect& r, int radius, color_t line, int thickness = 1);

// 面板 = 圆角填充 + 描边
void panel(PIMAGE img, const Rect& r, color_t fill, color_t border,
           int radius = theme::kRadius);

// 面板 + 向下投影（用几层递减透明感的描边近似，避免额外的位图运算）
void raisedPanel(PIMAGE img, const Rect& r, color_t fill, color_t border,
                 int radius = theme::kRadius, int depth = 3);

void divider(PIMAGE img, const Rect& r, color_t c = theme::kLine);

// ---------------------------------------------------------------------------
//  控件
// ---------------------------------------------------------------------------

enum class State : uint8_t
{
    Normal = 0,
    Hover,
    Active,     // 按下
    Selected,
    Disabled,
};

// 按钮。selected / hovered 由调用方根据交互状态传入。
void button(PIMAGE img, const Rect& r, const char* label, State st,
            color_t accent = theme::kAccent);

// 小圆角标签（用于"黑先""等级 3"这类元信息）
void chip(PIMAGE img, const Rect& r, const char* label, bool on,
          color_t accent = theme::kAccent);

// ---------------------------------------------------------------------------
//  数据可视化
// ---------------------------------------------------------------------------

// 单色进度条 / 计量条。value 会被夹到 [0,1]。
void meter(PIMAGE img, const Rect& r, double value, color_t fill,
           color_t track = theme::kPanelAlt);

// 双方占比条（胜率条、子数比条）：左段 firstShare，右段其余部分。
void meterSplit(PIMAGE img, const Rect& r, double firstShare,
                color_t firstColor, color_t secondColor,
                color_t track = theme::kPanelAlt);

// ---------------------------------------------------------------------------
//  提示叠加
// ---------------------------------------------------------------------------

// 提示类型 -> 颜色
color_t hintColor(HintKind kind);

// 在 (cx,cy) 处以半径 radius 画提示标记（圆点 / 叉 / 圈）
void hintMarker(PIMAGE img, int cx, int cy, int radius, HintKind kind, float weight);

// 从起点到终点的箭头（用于推荐着法）
void arrow(PIMAGE img, int x1, int y1, int x2, int y2, color_t c, int thickness = 3);

} // namespace ui
} // namespace chess
