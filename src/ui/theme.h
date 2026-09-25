#pragma once

#include <graphics.h>

namespace chess {

// ---------------------------------------------------------------------------
//  配色与尺寸
// ---------------------------------------------------------------------------
//  深色面板风格：棋盘用木色系（图集里的 chineseChess 木纹 / 黑白棋子），
//  外围 UI 用低饱和深灰蓝，保证棋子与提示色是画面上最醒目的对比。
// ---------------------------------------------------------------------------
namespace theme {

// ---- 背景层次 ----
constexpr color_t kBg        = EGERGB(0x1B, 0x1F, 0x26);   // 窗口底
constexpr color_t kPanel     = EGERGB(0x22, 0x27, 0x30);   // 面板
constexpr color_t kPanelAlt  = EGERGB(0x2A, 0x30, 0x3B);   // 面板内的次级块
constexpr color_t kPanelHi   = EGERGB(0x33, 0x3B, 0x48);   // hover / 选中
constexpr color_t kLine      = EGERGB(0x3A, 0x42, 0x50);   // 描边
constexpr color_t kLineSoft  = EGERGB(0x2E, 0x35, 0x41);   // 弱描边

// ---- 文字 ----
constexpr color_t kText      = EGERGB(0xE6, 0xEA, 0xF2);
constexpr color_t kTextDim   = EGERGB(0xA8, 0xB2, 0xC4);
constexpr color_t kTextFaint = EGERGB(0x6E, 0x7A, 0x8E);
constexpr color_t kTextOnAcc = EGERGB(0x10, 0x14, 0x1A);

// ---- 强调色（单一强调色 + 语义色）----
constexpr color_t kAccent    = EGERGB(0x4C, 0xA0, 0xF0);   // 主强调（蓝）
constexpr color_t kAccentDim = EGERGB(0x2E, 0x5F, 0x92);
constexpr color_t kGood      = EGERGB(0x4C, 0xC0, 0x84);   // 有利
constexpr color_t kWarn      = EGERGB(0xF2, 0xB1, 0x4B);   // 危险
constexpr color_t kDanger    = EGERGB(0xE5, 0x6C, 0x5C);   // 威胁 / 非法
constexpr color_t kNeutral   = EGERGB(0x8E, 0x9A, 0xAE);

// ---- 棋盘 ----
constexpr color_t kBoardBg     = EGERGB(0xE8, 0xC9, 0x8A);   // 木色底
constexpr color_t kBoardLine   = EGERGB(0x7A, 0x5A, 0x36);   // 棋盘线
constexpr color_t kBoardEdge   = EGERGB(0x5A, 0x42, 0x28);   // 棋盘外框
constexpr color_t kBoardShadow = EGERGB(0x14, 0x17, 0x1D);   // 棋盘投影

// ---- 双方棋子代表色 ----
// 用于胜率条、计量条、HUD 圆盘等"按方着色"的地方。
// 与图集里的黑/白棋子观感保持一致；象棋会覆盖为红/黑。
constexpr color_t kSideFirst  = EGERGB(0x2A, 0x2E, 0x34);
constexpr color_t kSideSecond = EGERGB(0xF2, 0xF4, 0xF8);

// ---- 尺寸 ----
constexpr int kTopBarH   = 72;
constexpr int kHudW      = 360;
constexpr int kPad       = 16;
constexpr int kRadius    = 8;
constexpr int kRadiusSm  = 5;

// ---- 字号 ----
//  ⚠️ 这里的数字是 GDI 的 **lfHeight（字符单元高度 = 字形 + 行距）**，
//     不是字形本身的尺寸。Microsoft YaHei 的行距约占 25%，所以
//     实际汉字边长 ≈ px * 0.75：setfont(19) 画出来是 ~14px 见方。
//     这就是"明明写了 19 却觉得字小"的原因。
//
//     实测了 lfHeight -> 字形宽度的完整曲线（--ui-layout 会重新打印）：
//         13:8  14:9  15:9  16:10 17:12 18:12 19:13 20:14 21:16 22:16
//         23:17 24:18 25:19 26:19 27:20 28:21 29:21 30:22 31:24 32:25
//         33:25 34:25   <- 32 以后基本不再涨，再大也白搭
//
//     换算经验值：想要 N px 见方的汉字，px 取表里最接近 N 的那个值。
//
// 用途：kFontTitle=顶栏标题，kFontBig=游戏名/行棋方，kFontBase=正文，
//       kFontSmall=按钮/说明，kFontTiny=次要信息与 chip
constexpr int kFontTitle = 32;   // 字形 25px，已是本字体在 32px 单元格下的上限
constexpr int kFontBig   = 26;   // 字形 19px
constexpr int kFontBase  = 21;   // 字形 16px
constexpr int kFontSmall = 20;   // 字形 14px
constexpr int kFontTiny  = 18;   // 字形 12px

// ---- 行距 ----
// 换行排版时用：约为 lfHeight 的 1.45 倍（≈ 字形的 1.9 倍），
// 中文字形本身较满，再紧会显得挤。
inline constexpr int lineHeightFor(int fontPx) { return fontPx * 145 / 100; }

} // namespace theme

// 语义化配色查询：提示/评估这类"有正负含义"的值统一走这里，
// 避免各处各写一套阈值。
inline color_t positiveColor() { return theme::kGood; }
inline color_t negativeColor() { return theme::kDanger; }
inline color_t neutralColor()  { return theme::kNeutral; }

} // namespace chess
