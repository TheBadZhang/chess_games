#pragma once

#include "core/types.h"

#include <graphics.h>

#include <string>
#include <vector>

namespace chess {

enum class Align : uint8_t
{
    Left = 0,
    Center,
    Right,
};

enum class VAlign : uint8_t
{
    Top = 0,
    Middle,
    Bottom,
};

// ---------------------------------------------------------------------------
//  文本层
// ---------------------------------------------------------------------------
//  本项目所有界面文案都是 **GBK 编码的窄字符串**（见 xmake.lua 的 text_charset
//  选项与 core/config.h 的说明），EGE 侧再按 getcodepage() 转成宽字符交给
//  DrawTextW。这里统一封装字体设定、测量与对齐绘制，避免各处散落 setfont /
//  outtextxy 以及重复踩"忘记 setbkmode(TRANSPARENT) 导致文字带底色块"的坑。
//
//  所有接口都显式接收目标 PIMAGE（默认 NULL = 窗口），理由见 ui/gfx_util.h：
//  离屏绘制时省略 pimg 容易出现"改到的不是同一张图"的问题。
// ---------------------------------------------------------------------------
namespace text {

// 设置字体。px 为像素字号；bold 走 weight 参数（FW_BOLD）。
// 内部缓存 (目标图, 字号, 粗体) 组合，重复设置相同字体会被跳过。
void setFont(int px, bool bold = false, PIMAGE img = nullptr);

// 当前生效的字号（用于光学居中计算）
int currentPx();

int width(const char* s, PIMAGE img = nullptr);
int height(const char* s, PIMAGE img = nullptr);

// 以左上角定位绘制
void draw(const char* s, int x, int y, PIMAGE img = nullptr);

// 在矩形内按对齐方式绘制
void drawIn(const char* s, const Rect& r, Align a = Align::Left,
            VAlign v = VAlign::Middle, PIMAGE img = nullptr);

// 以 (cx, cy) 为中心绘制（棋盘坐标、着点标记用）
void drawCentered(const char* s, int cx, int cy, PIMAGE img = nullptr);

// ---------------------------------------------------------------------------
//  换行排版
// ---------------------------------------------------------------------------
//  为什么要自己换行：UI 里不少文案是固定宽度的容器（游戏说明、HUD 的分析结论），
//  字号一调大就会溢出容器、和相邻元素叠在一起。
//
//  ⚠️ 关键点：**换行不能从多字节字符中间切开**。
//     本项目默认按 GBK 生成字面量（一个汉字 2 字节，首字节 >= 0x81），
//     如果按字节切，就会把汉字劈成两半 —— 屏幕上立刻变成乱码。
//     （若改用 --text_charset=utf8，一个汉字是 3 字节，按字节切同样会坏。）
//     因此这里的做法是：先按当前代码页把窄字符串转成**宽字符**，
//     在宽字符层面逐字断行，再交给宽字符绘制 API。
//     这样与编码方式无关，两种 charset 设置下都安全。
// ---------------------------------------------------------------------------

// 按像素宽度把文本断成若干行（每行都是完整的字符，不会切开多字节序列）
std::vector<std::wstring> wrapToWidth(const char* s, int maxWidth, PIMAGE img = nullptr);

// 在矩形内换行绘制。lineHeight <= 0 时按当前字号自动取。
// 返回实际绘制的行数。
int drawWrapped(const char* s, const Rect& r, Align a = Align::Left,
                int lineHeight = 0, PIMAGE img = nullptr);

// 换行后需要的高度（用于事先判断能否放下）
int wrappedHeight(const char* s, int maxWidth, int lineHeight = 0, PIMAGE img = nullptr);

// 按当前代码页把窄字符串转成宽字符串。
// 换行、以及任何需要"按字符而不是按字节"处理的场合都应该走这里 ——
// 直接用字节索引会切开 GBK/UTF-8 的多字节字符。
std::wstring toWide(const char* s);

// 垂直方向的光学微调量（像素）。EGE 的 textheight 含行距，直接按矩形居中会
// 略微偏下；默认 -1，如果实际观感不对只需改这一个数。
void setVerticalBias(int biasPx);
int  verticalBias();

} // namespace text
} // namespace chess
