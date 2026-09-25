#pragma once

#include "app/scene.h"
#include "games/registry.h"

namespace chess {

// ---------------------------------------------------------------------------
//  图集画廊布局常量
// ---------------------------------------------------------------------------
//  与 menu_scene 同样的理由：放在头文件里，让 --ui-layout 自检能用同一份数值
//  去校验字号调大后“精灵名一行”还塞不塞得进格子。
namespace atlas_layout {

constexpr int kCols      = 7;    // 每行多少个
constexpr int kStepX     = 172;  // 水平步距
constexpr int kStepY     = 178;  // 垂直步距
constexpr int kBoxW      = 158;  // 格子宽
constexpr int kBoxH      = 164;  // 格子高
constexpr int kArtSz     = 96;   // 图例绘制尺寸
constexpr int kPadX      = 10;   // 格子内左右内边距
constexpr int kArtY      = 26;   // 图例相对格子顶部
constexpr int kTitleY    = 4;    // 精灵名相对格子顶部
constexpr int kCoordY    = 130;  // 精灵像素矩形相对格子顶部

// 文字可用宽度
constexpr int kTitleW = kBoxW - 2 * kPadX;

} // namespace atlas_layout

// ---------------------------------------------------------------------------
//  图集查看器
// ---------------------------------------------------------------------------
//  两种视图（Tab / F1 切换）：
//    * 画廊：每个精灵放大排列，标注名字与像素矩形
//    * 校准：整张图集按 unit 网格放大，叠加单元坐标与"名字 -> 图案"边框
//
//  这是 Phase 0 用来敲定精灵映射的工具，保留在菜单里长期可用：
//  以后换素材（chesses200/300/400）或加新棋子时，第一件事就是来这里核对。
// ---------------------------------------------------------------------------
class AtlasScene : public Scene
{
public:
    const char* name() const override { return "图集查看器"; }

    void onEnter(App& app) override;
    void update(App& app, double dtMs) override;
    void draw(App& app, PIMAGE img) override;
    bool onMouse(App& app, const mouse_msg& m) override;
    bool onKey(App& app, const key_msg& k) override;
    std::string topBarInfo() const override;

private:
    void drawGallery(App& app, PIMAGE img);
    void drawCalibration(App& app, PIMAGE img);

    bool showCalibration_ = false;
    int  hoverIndex_      = -1;
};

} // namespace chess
