#include "ui/text.h"

#include "core/config.h"
#include "ui/theme.h"

#include <cstring>

namespace chess {
namespace text {

namespace {

// 字体缓存：EGE 的 setfont 会创建/选择 GDI 字体对象，逐次调用有成本。
// 缓存键必须包含目标图 —— 字体是挂在各个 IMAGE 自己的 DC 上的，
// 切换目标图后即使参数相同也必须重新设置。
struct FontKey
{
    PIMAGE img  = nullptr;
    int    px   = -1;
    bool   bold = false;

    bool operator==(const FontKey& o) const
    {
        return img == o.img && px == o.px && bold == o.bold;
    }
};

FontKey g_current{};
int     g_bias = -1;

void applyFont(const FontKey& key)
{
    // 去掉行距干扰，让文字按字形本身定位
    setbkmode(TRANSPARENT, key.img);

    if (key.bold)
    {
        // 带 weight 的重载：setfont(height, width, typeface, escapement,
        // orientation, weight, italic, underline, strikeOut, pimg)
        setfont(key.px, 0, kUiFontFace(), 0, 0, FW_BOLD, false, false, false, key.img);
    }
    else
    {
        setfont(key.px, 0, kUiFontFace(), key.img);
    }

    g_current = key;
}

} // namespace

void setFont(int px, bool bold, PIMAGE img)
{
    if (px <= 0)
    {
        px = theme::kFontBase;
    }

    const FontKey key{img, px, bold};
    if (key == g_current)
    {
        return;
    }
    applyFont(key);
}

int currentPx()
{
    return g_current.px > 0 ? g_current.px : theme::kFontBase;
}

int width(const char* s, PIMAGE img)
{
    if (!s || !*s)
    {
        return 0;
    }
    return textwidth(s, img);
}

int height(const char* s, PIMAGE img)
{
    if (!s || !*s)
    {
        return 0;
    }
    return textheight(s, img);
}

void draw(const char* s, int x, int y, PIMAGE img)
{
    if (!s || !*s)
    {
        return;
    }
    outtextxy(x, y, s, img);
}

void drawIn(const char* s, const Rect& r, Align a, VAlign v, PIMAGE img)
{
    if (!s || !*s || r.empty())
    {
        return;
    }

    const int tw = textwidth(s, img);
    const int th = textheight(s, img);

    int tx = r.x;
    switch (a)
    {
    case Align::Center: tx = r.x + (r.w - tw) / 2; break;
    case Align::Right:  tx = r.right() - tw; break;
    default: break;
    }

    int ty = r.y;
    switch (v)
    {
    case VAlign::Middle: ty = r.y + (r.h - th) / 2; break;
    case VAlign::Bottom: ty = r.bottom() - th; break;
    default: break;
    }

    outtextxy(tx, ty + g_bias, s, img);
}

void drawCentered(const char* s, int cx, int cy, PIMAGE img)
{
    if (!s || !*s)
    {
        return;
    }
    const int tw = textwidth(s, img);
    const int th = textheight(s, img);
    outtextxy(cx - tw / 2, cy - th / 2 + g_bias, s, img);
}

void setVerticalBias(int biasPx)
{
    g_bias = biasPx;
}

int verticalBias()
{
    return g_bias;
}

// ---------------------------------------------------------------------------
//  换行排版
// ---------------------------------------------------------------------------

std::wstring toWide(const char* s)
{
    if (!s || !*s)
    {
        return {};
    }

    // 用 EGE 当前的代码页做转换 —— 与窄字符绘制路径用的是同一套规则，
    // 所以转换结果必然与"EGE 自己会怎么解释这个串"一致。
    const UINT cp   = getcodepage();
    const int  need = MultiByteToWideChar(cp, 0, s, -1, nullptr, 0);
    if (need <= 1)
    {
        return {};
    }

    std::wstring out(static_cast<size_t>(need - 1), L'\0');
    MultiByteToWideChar(cp, 0, s, -1, &out[0], need);
    return out;
}

std::vector<std::wstring> wrapToWidth(const char* s, int maxWidth, PIMAGE img)
{
    std::vector<std::wstring> lines;

    const std::wstring w = toWide(s);
    if (w.empty())
    {
        return lines;
    }

    // 宽度不合理时退化为"不换行"，至少不会把文本吞掉
    if (maxWidth <= 0)
    {
        lines.push_back(w);
        return lines;
    }

    // 逐字累加、超宽就断行。
    // 在**宽字符**层面断行是关键：一个 wchar_t 就是一个完整字符，
    // 不可能像按字节处理那样把 GBK/UTF-8 的多字节序列切开。
    std::wstring cur;
    std::wstring probe;
    probe.reserve(w.size() + 1);

    for (wchar_t ch : w)
    {
        // 显式的换行符：强制断行
        if (ch == L'\n')
        {
            lines.push_back(cur);
            cur.clear();
            continue;
        }

        probe = cur;
        probe.push_back(ch);

        if (!cur.empty() && textwidth(probe.c_str(), img) > maxWidth)
        {
            // 放不下 -> 当前行结束，ch 另起一行
            lines.push_back(cur);
            cur.clear();
            cur.push_back(ch);
        }
        else
        {
            cur.push_back(ch);
        }
    }

    if (!cur.empty() || lines.empty())
    {
        lines.push_back(cur);
    }
    return lines;
}

int drawWrapped(const char* s, const Rect& r, Align a, int lineHeight, PIMAGE img)
{
    if (!s || !*s || r.empty())
    {
        return 0;
    }

    if (lineHeight <= 0)
    {
        lineHeight = theme::lineHeightFor(currentPx());
    }

    const std::vector<std::wstring> lines = wrapToWidth(s, r.w, img);

    int y = r.y;
    int drawn = 0;
    for (const std::wstring& line : lines)
    {
        // 超出矩形底部就不再画：宁可少显示，也不要叠到下面的元素上
        if (y + lineHeight > r.bottom() + 1)
        {
            break;
        }
        if (!line.empty())
        {
            const int tw = textwidth(line.c_str(), img);
            int       tx = r.x;
            switch (a)
            {
            case Align::Center: tx = r.x + (r.w - tw) / 2; break;
            case Align::Right:  tx = r.right() - tw; break;
            default: break;
            }
            outtextxy(tx, y + g_bias, line.c_str(), img);
        }
        y += lineHeight;
        ++drawn;
    }
    return drawn;
}

int wrappedHeight(const char* s, int maxWidth, int lineHeight, PIMAGE img)
{
    if (lineHeight <= 0)
    {
        lineHeight = theme::lineHeightFor(currentPx());
    }
    return static_cast<int>(wrapToWidth(s, maxWidth, img).size()) * lineHeight;
}

} // namespace text
} // namespace chess
