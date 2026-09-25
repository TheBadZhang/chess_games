#pragma once

#include <graphics.h>

namespace chess {

// ---------------------------------------------------------------------------
//  修复 GDI 绘制路径遗留的 alpha=0 像素
// ---------------------------------------------------------------------------
//  背景（EGE 25.11）：
//    * EGE 的 IMAGE 像素是 **预乘 alpha** 的 BGRA（color_t = 0xAARRGGBB）。
//    * 一部分绘制函数直接写像素缓冲，alpha 正确：
//        cleardevice()、putimage_alpha*()、putpixel_f() ...
//    * 另一部分走 GDI，只写 RGB、**不碰第 4 字节**，于是 alpha 留成 0：
//        bar()、bar3d()、rectangle()、line()、outtextxy()、fillellipse() ...
//      见 egegapi.cpp: bar() 内部用的是 FillRect + DC 当前画刷。
//
//  后果（两个都很隐蔽）：
//    1) savepng(img, path, false) 会做 color_unpremultiply()，alpha=0 时结果恒为 0
//       —— 整片区域**变成纯黑**，看起来像"填充色没生效"。
//    2) 把这张图 putimage_withalpha() 合成到别处时，这些像素被当成**全透明**，
//       内容直接消失。
//
//  窗口（pimg = NULL）不受影响：窗口缓冲是用 BitBlt 呈现的，alpha 不参与。
//  所以这个工具只在"离屏 IMAGE -> 合成/保存"的场景里需要调用。
//
//  做法：把 alpha 为 0 的像素补成不透明（只补 0，不动已有的半透明像素，
//  以免破坏抗锯齿边缘的预乘关系）。
void makeImageOpaque(PIMAGE img);

} // namespace chess
