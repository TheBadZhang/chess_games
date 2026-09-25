#include "ui/painter.h"

#include "ui/text.h"

#include <algorithm>
#include <cmath>

namespace chess {
namespace ui {

// ---------------------------------------------------------------------------
//  基础图元
// ---------------------------------------------------------------------------

void fillBox(PIMAGE img, const Rect& r, color_t fill)
{
    if (r.empty())
    {
        return;
    }
    setfillcolor(fill, img);
    bar(r.x, r.y, r.right() - 1, r.bottom() - 1, img);
}

void strokeBox(PIMAGE img, const Rect& r, color_t line, int thickness)
{
    if (r.empty())
    {
        return;
    }
    setcolor(line, img);
    setlinewidth(static_cast<float>(thickness), img);
    rectangle(r.x, r.y, r.right() - 1, r.bottom() - 1, img);
    setlinewidth(1.0f, img);
}

void fillRound(PIMAGE img, const Rect& r, int radius, color_t fill)
{
    if (r.empty())
    {
        return;
    }
    if (radius <= 0)
    {
        fillBox(img, r, fill);
        return;
    }
    setfillcolor(fill, img);
    fillroundrect(r.x, r.y, r.right() - 1, r.bottom() - 1, radius, radius, img);
}

void strokeRound(PIMAGE img, const Rect& r, int radius, color_t line, int thickness)
{
    if (r.empty())
    {
        return;
    }
    setcolor(line, img);
    setlinewidth(static_cast<float>(thickness), img);
    if (radius <= 0)
    {
        rectangle(r.x, r.y, r.right() - 1, r.bottom() - 1, img);
    }
    else
    {
        roundrect(r.x, r.y, r.right() - 1, r.bottom() - 1, radius, radius, img);
    }
    setlinewidth(1.0f, img);
}

void panel(PIMAGE img, const Rect& r, color_t fill, color_t border, int radius)
{
    fillRound(img, r, radius, fill);
    strokeRound(img, r, radius, border, 1);
}

void raisedPanel(PIMAGE img, const Rect& r, color_t fill, color_t border,
                 int radius, int depth)
{
    // 用几层逐渐外扩、颜色由浅入深的圆角描边近似投影。
    // 比真投影便宜，且在这种深色扁平风格里观感差别很小。
    for (int i = depth; i >= 1; --i)
    {
        const Rect sr{r.x - i, r.y + i, r.w + 2 * i, r.h + 2 * i};
        const int  k = 40 - 8 * i;   // 越远越淡
        if (k <= 0)
        {
            continue;
        }
        strokeRound(img, sr, radius + i, EGERGB(k, k + 2, k + 6), 1);
    }
    panel(img, r, fill, border, radius);
}

void divider(PIMAGE img, const Rect& r, color_t c)
{
    fillBox(img, r, c);
}

// ---------------------------------------------------------------------------
//  控件
// ---------------------------------------------------------------------------

void button(PIMAGE img, const Rect& r, const char* label, State st, color_t accent)
{
    color_t fill   = theme::kPanelAlt;
    color_t border = theme::kLine;
    color_t fg     = theme::kText;

    switch (st)
    {
    case State::Hover:
        fill   = theme::kPanelHi;
        border = theme::kAccentDim;
        break;
    case State::Active:
        fill   = theme::kAccentDim;
        border = theme::kAccent;
        fg     = theme::kText;
        break;
    case State::Selected:
        fill   = accent;
        border = accent;
        fg     = theme::kTextOnAcc;
        break;
    case State::Disabled:
        fill   = theme::kPanel;
        border = theme::kLineSoft;
        fg     = theme::kTextFaint;
        break;
    default:
        break;
    }

    panel(img, r, fill, border, theme::kRadiusSm);
    text::setFont(theme::kFontSmall, st == State::Selected, img);
    setcolor(fg, img);
    text::drawIn(label, r, Align::Center, VAlign::Middle, img);
}

void chip(PIMAGE img, const Rect& r, const char* label, bool on, color_t accent)
{
    const color_t fill = on ? accent : theme::kPanelAlt;
    const color_t fg   = on ? theme::kTextOnAcc : theme::kTextDim;

    fillRound(img, r, r.h / 2, fill);
    if (!on)
    {
        strokeRound(img, r, r.h / 2, theme::kLine, 1);
    }
    text::setFont(theme::kFontTiny, false, img);
    setcolor(fg, img);
    text::drawIn(label, r, Align::Center, VAlign::Middle, img);
}

// ---------------------------------------------------------------------------
//  数据可视化
// ---------------------------------------------------------------------------

namespace {

void meterBody(PIMAGE img, const Rect& r, color_t track)
{
    fillRound(img, r, r.h / 2, track);
}

} // namespace

void meter(PIMAGE img, const Rect& r, double value, color_t fill, color_t track)
{
    if (r.empty())
    {
        return;
    }
    value = std::clamp(value, 0.0, 1.0);

    meterBody(img, r, track);
    const int w = static_cast<int>(std::lround(r.w * value));
    if (w > 0)
    {
        // 填充段至少保留一点宽度，否则极小值看起来像"完全没画"
        const Rect fr{r.x, r.y, std::max(w, r.h), r.h};
        fillRound(img, fr, r.h / 2, fill);
    }
    strokeRound(img, r, r.h / 2, theme::kLineSoft, 1);
}

void meterSplit(PIMAGE img, const Rect& r, double firstShare,
                color_t firstColor, color_t secondColor, color_t track)
{
    if (r.empty())
    {
        return;
    }
    firstShare = std::clamp(firstShare, 0.0, 1.0);

    meterBody(img, r, track);
    const int w1 = static_cast<int>(std::lround(r.w * firstShare));
    if (w1 > 0)
    {
        fillRound(img, Rect{r.x, r.y, w1, r.h}, r.h / 2, firstColor);
    }
    if (w1 < r.w)
    {
        fillRound(img, Rect{r.right() - (r.w - w1), r.y, r.w - w1, r.h}, r.h / 2, secondColor);
    }
    strokeRound(img, r, r.h / 2, theme::kLineSoft, 1);
}

// ---------------------------------------------------------------------------
//  提示叠加
// ---------------------------------------------------------------------------

color_t hintColor(HintKind kind)
{
    switch (kind)
    {
    case HintKind::Legal:       return EGERGB(0xE6, 0xEA, 0xF2);
    case HintKind::Illegal:     return EGERGB(0x5A, 0x62, 0x70);
    case HintKind::Good:        return theme::kGood;
    case HintKind::Danger:      return theme::kWarn;
    case HintKind::Threat:      return theme::kDanger;
    case HintKind::Recommended: return theme::kAccent;
    default:                    return theme::kNeutral;
    }
}

void hintMarker(PIMAGE img, int cx, int cy, int radius, HintKind kind, float weight)
{
    const color_t c = hintColor(kind);
    const int     rr = std::max(2, radius);

    switch (kind)
    {
    case HintKind::Legal:
    {
        // 小实心圆点：空点用 0.55 的权重，权重直接体现"多好走"
        const int r = std::max(2, static_cast<int>(rr * 0.36f));
        setfillcolor(c, img);
        fillellipse(cx, cy, r, r, img);
        break;
    }

    case HintKind::Illegal:
    {
        // 灰叉
        const int d = std::max(3, static_cast<int>(rr * 0.42f));
        setcolor(c, img);
        setlinewidth(2.0f, img);
        line(cx - d, cy - d, cx + d, cy + d, img);
        line(cx - d, cy + d, cx + d, cy - d, img);
        setlinewidth(1.0f, img);
        break;
    }

    case HintKind::Good:
    case HintKind::Danger:
    case HintKind::Threat:
    {
        // 半透明实心圆盘 + 描边。weight 参与半径，让"越重要越显眼"
        const double w = std::clamp(static_cast<double>(weight), 0.0, 1.0);
        const int    r = static_cast<int>(rr * (0.30 + 0.16 * w));
        setfillcolor(c, img);
        fillellipse(cx, cy, r, r, img);
        break;
    }

    case HintKind::Recommended:
    {
        // 高亮圆环（空心）
        setcolor(c, img);
        setlinewidth(3.0f, img);
        circle(cx, cy, std::max(3, static_cast<int>(rr * 0.62f)), img);
        setlinewidth(1.0f, img);
        break;
    }

    default:
        break;
    }
}

void arrow(PIMAGE img, int x1, int y1, int x2, int y2, color_t c, int thickness)
{
    const double dx = static_cast<double>(x2 - x1);
    const double dy = static_cast<double>(y2 - y1);
    const double len = std::hypot(dx, dy);
    if (len < 1.0)
    {
        return;
    }

    const double ux = dx / len;
    const double uy = dy / len;

    // 让箭杆不要顶到终点，给箭头留出空间
    const int headLen = std::max(8, thickness * 4);
    const int shaftX2 = static_cast<int>(std::lround(x2 - ux * headLen * 0.85));
    const int shaftY2 = static_cast<int>(std::lround(y2 - uy * headLen * 0.85));

    setcolor(c, img);
    setlinewidth(static_cast<float>(thickness), img);
    line(x1, y1, shaftX2, shaftY2, img);
    setlinewidth(1.0f, img);

    // 箭头头部：用 fillpoly 画三角形
    // (-uy, ux) 是垂直于箭头方向的单位向量
    const double px = -uy;
    const double py = ux;
    const double halfW = headLen * 0.42;

    const int pts[6] = {
        x2,
        y2,
        static_cast<int>(std::lround(x2 - ux * headLen + px * halfW)),
        static_cast<int>(std::lround(y2 - uy * headLen + py * halfW)),
        static_cast<int>(std::lround(x2 - ux * headLen - px * halfW)),
        static_cast<int>(std::lround(y2 - uy * headLen - py * halfW)),
    };
    setfillcolor(c, img);
    fillpoly(3, pts, img);
}

} // namespace ui
} // namespace chess
