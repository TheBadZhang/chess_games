#include "ui/atlas.h"

#include "core/config.h"
#include "ui/gfx_util.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace chess {

Atlas::~Atlas()
{
    if (image_)
    {
        delimage(image_);
        image_ = nullptr;
    }
}

bool Atlas::load(const std::string& imagePath,
                 const std::string& tablePath,
                 std::string*       err)
{
    auto fail = [err](const std::string& msg) {
        if (err)
        {
            *err = msg;
        }
        return false;
    };

    // ---- 读取精灵表 ----
    // 控制台/日志一律用 ASCII：VS Code 终端是 UTF-8，而本项目按 GBK 生成
    // 窄字符串，中文诊断信息在终端里会变成乱码，反而妨碍排查。
    std::ifstream file(tablePath);
    if (!file)
    {
        return fail("cannot open sprite table: " + tablePath);
    }

    std::vector<std::string>    names;
    std::map<std::string, SpriteRect> rects;
    std::string                 name;
    SpriteRect                  r;
    while (file >> name >> r.x >> r.y >> r.w >> r.h)
    {
        if (rects.find(name) != rects.end())
        {
            // 与旧 main.cc 的行为一致：重名直接报错，避免静默覆盖
            return fail("duplicated sprite name in table: " + name);
        }
        rects.emplace(name, r);
        names.push_back(name);
    }
    if (names.empty())
    {
        return fail("sprite table is empty or malformed: " + tablePath);
    }

    // ---- 载入图集图片 ----
    PIMAGE img = newimage();
    if (!img || getimage_pngfile(img, imagePath.c_str()) != grOk)
    {
        if (img)
        {
            delimage(img);
        }
        return fail("cannot load atlas image: " + imagePath);
    }

    image_ = img;
    width_ = getwidth(image_);
    height_ = getheight(image_);
    names_ = std::move(names);
    rects_ = std::move(rects);

    // 单位像素与素材不匹配时给出提示，便于换成 chesses200/300/400
    const int expectW = 7 * kUnitPixels;
    const int expectH = 3 * kUnitPixels;
    if (width_ < expectW || height_ < expectH)
    {
        std::printf("[atlas] warning: image %dx%d is smaller than expected %dx%d "
                    "(kUnitPixels=%d may not match this asset)\n",
                    width_, height_, expectW, expectH, kUnitPixels);
    }

    return true;
}

const SpriteRect* Atlas::find(const std::string& name) const
{
    auto it = rects_.find(name);
    return it == rects_.end() ? nullptr : &it->second;
}

SpriteRect Atlas::rect(const std::string& name) const
{
    if (const SpriteRect* p = find(name))
    {
        // 表里坐标是 32 单位制；kUnitPixels 变化时按比例换算
        if (kUnitPixels != 32)
        {
            const double s = static_cast<double>(kUnitPixels) / 32.0;
            return SpriteRect{
                static_cast<int>(p->x * s),
                static_cast<int>(p->y * s),
                static_cast<int>(p->w * s),
                static_cast<int>(p->h * s),
            };
        }
        return *p;
    }
    return SpriteRect{};
}

void Atlas::draw(const std::string& name, int x, int y, int size, PIMAGE dest) const
{
    const SpriteRect r = rect(name);
    if (!image_ || r.w <= 0 || r.h <= 0)
    {
        return;
    }
    // 缩放置绘（smooth=true 做双线性插值，避免像素块感）
    putimage_withalpha(dest, image_, x, y, size, size, r.x, r.y, r.w, r.h, true);
}

bool dumpAtlasSheet(const Atlas& atlas, const std::string& outPath)
{
    if (!atlas.image())
    {
        return false;
    }

    const auto& names = atlas.names();

    // ---- 版面计算 ----
    const int cols     = 7;
    const int cellW    = 168;
    const int cellH    = 208;
    const int rows     = static_cast<int>((names.size() + cols - 1) / cols);
    const int sheetW   = cols * cellW;
    const int sheetH   = rows * cellH;
    const int artSize  = 128;   // 精灵放大到 4 倍（32 -> 128）
    const int artX     = 20;
    const int artY     = 52;
    const int padX     = 14;

    PIMAGE sheet = newimage(sheetW, sheetH);
    if (!sheet)
    {
        return false;
    }

    // 重要：往 IMAGE 上绘制时，**一律显式把目标图像作为最后一个 pimg 参数传进去**，
    // 不要依赖 settarget() + 省略 pimg 的写法。
    // 实测（EGE 25.11）：settarget(sheet) 之后 cleardevice() 能生效，但 bar() 画出来
    // 仍是纯黑——因为 bar() 用的是 DC 里当前选中的画刷
    // （GetCurrentObject(m_hDC, OBJ_BRUSH)），而省略 pimg 时 setfillcolor 没能作用到
    // 这张 IMAGE 的 DC 上。显式传 pimg 可以彻底避开这类"改到的不是同一张图"的问题。
    const PIMAGE D = sheet;

    setbkcolor(EGERGB(0xF0, 0xF0, 0xF0), D);
    cleardevice(D);

    for (size_t i = 0; i < names.size(); ++i)
    {
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const int cx  = col * cellW;
        const int cy  = row * cellH;

        // 格子底板 + 边框
        setfillcolor(EGERGB(0xFF, 0xFF, 0xFF), D);
        bar(cx + 4, cy + 4, cx + cellW - 4, cy + cellH - 4, D);
        setcolor(EGERGB(0xC0, 0xC0, 0xC0), D);
        rectangle(cx + 4, cy + 4, cx + cellW - 4, cy + cellH - 4, D);

        // 索引 + 名字
        char line[128];
        std::snprintf(line, sizeof(line), "#%d  %s", static_cast<int>(i), names[i].c_str());
        setfont(18, 0, kUiFontFace(), D);
        setcolor(EGERGB(0x10, 0x10, 0x10), D);
        outtextxy(cx + padX, cy + 8, line, D);

        // 原始矩形
        const SpriteRect r = atlas.rect(names[i]);
        std::snprintf(line, sizeof(line), "rect %d,%d %dx%d", r.x, r.y, r.w, r.h);
        setfont(14, 0, kUiFontFace(), D);
        setcolor(EGERGB(0x80, 0x80, 0x80), D);
        outtextxy(cx + padX, cy + 30, line, D);

        // 放大后的图案（画在浅色棋盘格底纹上，便于看清透明区域）
        for (int gx = 0; gx < artSize; gx += 16)
        {
            for (int gy = 0; gy < artSize; gy += 16)
            {
                const bool alt = (((gx / 16) + (gy / 16)) % 2) == 0;
                setfillcolor(alt ? EGERGB(0xE8, 0xE8, 0xE8) : EGERGB(0xD8, 0xD8, 0xD8), D);
                bar(cx + padX + gx, cy + artY + gy,
                    cx + padX + gx + 15, cy + artY + gy + 15, D);
            }
        }
        putimage_withalpha(D, atlas.image(), cx + padX, cy + artY,
                           artSize, artSize, r.x, r.y, r.w, r.h, true);
        setcolor(EGERGB(0x90, 0x90, 0x90), D);
        rectangle(cx + padX, cy + artY, cx + padX + artSize, cy + artY + artSize, D);
    }

    // 关键：bar()/rectangle()/outtextxy() 都是 GDI 绘制，不会写入 alpha 字节，
    // 而 EGE 像素是预乘 alpha 的。不补齐的话 savepng() 会把整片区域反预乘成纯黑。
    // 详见 ui/gfx_util.h 的说明。
    makeImageOpaque(sheet);

    const bool ok = savepng(sheet, outPath.c_str(), false) == grOk;

    // 采样 sheet 自身的像素，把 savepng 从排查链路里摘出去：
    // 如果这里读到的是正确颜色，而 PNG 里不对，那问题就在 savepng/alpha 通道上。
    {
        struct Probe
        {
            const char* what;
            int         x, y;
        };
        const Probe probes[] = {
            {"cell bg (expect FFFFFF) ", 8, 8},
            {"border  (expect F0F0F0) ", 1174, 622},
            {"checker (expect E8/D8)  ", 20, 60},
        };
        for (const Probe& p : probes)
        {
            const color_t c = getpixel(p.x, p.y, sheet);
            std::printf("[atlas] probe %s at(%4d,%3d) = 0x%08X\n",
                        p.what, p.x, p.y, static_cast<unsigned>(c));
        }
    }

    delimage(sheet);
    return ok;
}

} // namespace chess
