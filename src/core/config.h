#pragma once

// ============================================================================
//  项目级编译期配置
// ----------------------------------------------------------------------------
//  xmake.lua 通过 option("text_charset") 注入 CHESS_TEXT_CODEPAGE；
//  这里给出兜底定义，使代码脱离 xmake 直接编译时也能工作。
// ============================================================================

#include <ege.h>   // 需要 EGE_CODEPAGE_* 常量

#ifndef CHESS_TEXT_CODEPAGE
#define CHESS_TEXT_CODEPAGE EGE_CODEPAGE_ANSI
#endif

namespace chess {

// 界面用字体的 ASCII 字体名。
// 刻意不用中文字体名（如 "宋体"）：EGE 的 font.cpp 里 lfFaceName 的转换
// 沿用的是历史 ANSI 行为，用纯 ASCII 名可以完全绕开这个不确定性。
inline const char* kUiFontFace() { return "Microsoft YaHei"; }

// 窗口尺寸（固定 1280x800）
inline constexpr int kWindowWidth  = 1280;
inline constexpr int kWindowHeight = 800;

} // namespace chess
