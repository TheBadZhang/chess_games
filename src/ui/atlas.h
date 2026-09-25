#pragma once

#include <graphics.h>

#include <map>
#include <string>
#include <vector>

namespace chess {

struct SpriteRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// ---------------------------------------------------------------------------
//  棋子图集
// ---------------------------------------------------------------------------
//  res/chesses.txt 用「32 单位制」的整数格描述每个精灵的位置，而
//  res/chesses100.png 的每格恰好是 32px（224x96 = 7 列 x 3 行），
//  所以表里的坐标与像素 **1:1 直接对应**，不需要任何换算。
//
//  res/chesses200/300/400.png 是同一布局的 2x/3x/4x 放大版，
//  只要把 kUnitPixels 改成 64/96/128 就能整体换成更清晰的素材。
//
//  注意：这里不假设精灵一定要铺满棋盘格——draw() 一律做缩放置绘，
//        因此任意棋盘格尺寸都能正确适配。
// ---------------------------------------------------------------------------
class Atlas
{
public:
    // res/chesses100.png 每个单位对应的像素数
    static constexpr int kUnitPixels = 32;

    Atlas() = default;
    ~Atlas();

    Atlas(const Atlas&)            = delete;
    Atlas& operator=(const Atlas&) = delete;

    // 加载图集；失败时返回 false 并把原因写入 err（可为 nullptr）
    bool load(const std::string& imagePath,
              const std::string& tablePath,
              std::string*       err = nullptr);

    const SpriteRect* find(const std::string& name) const;

    bool has(const std::string& name) const { return find(name) != nullptr; }

    // 缺图时的兜底：返回 {0,0,0,0}，调用方据此改用文字/图形绘制
    SpriteRect rect(const std::string& name) const;

    const std::vector<std::string>& names() const { return names_; }

    PIMAGE image() const { return image_; }
    int    width() const { return width_; }
    int    height() const { return height_; }

    // 把精灵缩放置绘到 (x, y) 起点、边长为 size 的方格里。
    // dest 必须显式指定（默认窗口）—— 离屏渲染时若写死成窗口，
    // 精灵会画到窗口缓存而不是目标图，导致"棋盘格有、棋子没有"或
    // 只在窗口上重复叠加一层。
    void draw(const std::string& name, int x, int y, int size,
              PIMAGE dest = nullptr) const;

private:
    PIMAGE                      image_ = nullptr;
    int                         width_ = 0;
    int                         height_ = 0;
    std::vector<std::string>    names_;
    std::map<std::string, SpriteRect> rects_;
};

// 把图集渲染成一张「带标注的对照图」并保存为 PNG。
// 供 `chess --atlas-dump` 使用：可以直接看图确认每个名字到底对应哪个图案，
// 而不必启动图形界面逐个点击。
bool dumpAtlasSheet(const Atlas& atlas, const std::string& outPath);

} // namespace chess
