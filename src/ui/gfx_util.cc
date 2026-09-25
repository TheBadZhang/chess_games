#include "ui/gfx_util.h"

namespace chess {

void makeImageOpaque(PIMAGE img)
{
    if (!img)
    {
        return;
    }

    const int w = getwidth(img);
    const int h = getheight(img);

    // 只把完全透明的像素补成不透明；半透明像素保持原样，
    // 否则会破坏 putimage_* 写进去的预乘 alpha 关系（边缘会变亮）。
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const color_t c = getpixel_f(x, y, img);
            if ((c >> 24) == 0)
            {
                // putpixel_f 直接写 m_pBuffer，因此能真正写进 alpha 字节
                // （putpixel 走的是别的路径，不保证写 alpha）
                putpixel_f(x, y, c | 0xFF000000u, img);
            }
        }
    }
}

} // namespace chess
