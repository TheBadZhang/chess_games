#include "app/atlas_scene.h"

#include "app/app.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <cstdio>

namespace chess {

void AtlasScene::onEnter(App&)
{
    showCalibration_ = false;
    hoverIndex_      = -1;
}

void AtlasScene::update(App& app, double)
{
    const Rect area = app.content();
    const int  cols = showCalibration_ ? 1 : 7;
    const int  cellW = showCalibration_ ? area.w : 172;
    (void)cols;
    (void)cellW;

    // 画廊视图下的 hover 反馈
    if (!showCalibration_)
    {
        hoverIndex_ = -1;
        const Atlas& atlas = app.atlas();
        const int originX = area.x + 34;
        const int originY = area.y + 40;
        for (size_t i = 0; i < atlas.names().size(); ++i)
        {
            const int col = static_cast<int>(i) % 7;
            const int row = static_cast<int>(i) / 7;
            const Rect r{originX + col * 172, originY + row * 178, 158, 164};
            if (r.contains(app.mouseX(), app.mouseY()))
            {
                hoverIndex_ = static_cast<int>(i);
                break;
            }
        }
    }
}

void AtlasScene::drawGallery(App& app, PIMAGE img)
{
    const Atlas& atlas = app.atlas();
    const auto&  names = atlas.names();

    const Rect area    = app.content();
    const int  originX = area.x + 34;
    const int  originY = area.y + 40;
    const int  artSz   = atlas_layout::kArtSz;

    for (size_t i = 0; i < names.size(); ++i)
    {
        const int col = static_cast<int>(i) % atlas_layout::kCols;
        const int row = static_cast<int>(i) / atlas_layout::kCols;
        const int x   = originX + col * atlas_layout::kStepX;
        const int y   = originY + row * atlas_layout::kStepY;

        const bool hov = (static_cast<int>(i) == hoverIndex_);
        const Rect box{x, y, atlas_layout::kBoxW, atlas_layout::kBoxH};

        ui::panel(img, box, hov ? theme::kPanelHi : theme::kPanel,
                  hov ? theme::kAccent : theme::kLine, theme::kRadius);

        // 棋盘格底纹：便于看清透明区域
        for (int gx = 0; gx < artSz; gx += 16)
        {
            for (int gy = 0; gy < artSz; gy += 16)
            {
                const bool alt = (((gx / 16) + (gy / 16)) % 2) == 0;
                ui::fillBox(img, Rect{x + atlas_layout::kPadX + gx,
                                      y + atlas_layout::kArtY + gy, 16, 16},
                            alt ? theme::kPanelAlt : theme::kPanel);
            }
        }

        const SpriteRect r = atlas.rect(names[i]);
        putimage_withalpha(img, atlas.image(), x + atlas_layout::kPadX, y + atlas_layout::kArtY,
                           artSz, artSz, r.x, r.y, r.w, r.h, true);

        // 精灵名：名字长度不可控（来自 chesses.txt），字号调大后可能超出格子，
        // 所以用最小的一档（kFontTiny）。--ui-layout 会断言"最长的那个名字"
        // 在这个宽度下确实放得下，换素材时会立刻发现。
        char line[128];
        std::snprintf(line, sizeof(line), "#%d  %s", static_cast<int>(i), names[i].c_str());
        text::setFont(theme::kFontTiny, true, img);
        setcolor(theme::kText, img);
        text::draw(line, x + atlas_layout::kPadX, y + atlas_layout::kTitleY, img);

        std::snprintf(line, sizeof(line), "%d,%d  %dx%d", r.x, r.y, r.w, r.h);
        text::setFont(theme::kFontTiny, false, img);
        setcolor(theme::kTextFaint, img);
        text::draw(line, x + atlas_layout::kPadX, y + atlas_layout::kCoordY, img);
    }
}

void AtlasScene::drawCalibration(App& app, PIMAGE img)
{
    const Atlas& atlas = app.atlas();

    constexpr int kScale = 4;
    const int originX = app.content().x + 34;
    const int originY = app.content().y + 40;

    const int aw = atlas.width();
    const int ah = atlas.height();
    const int uw = Atlas::kUnitPixels;

    const int dw = aw * kScale;
    const int dh = ah * kScale;

    for (int gx = 0; gx < dw; gx += 32)
    {
        for (int gy = 0; gy < dh; gy += 32)
        {
            const bool alt = (((gx / 32) + (gy / 32)) % 2) == 0;
            ui::fillBox(img, Rect{originX + gx, originY + gy, 32, 32},
                        alt ? theme::kPanelAlt : theme::kPanel);
        }
    }

    putimage_withalpha(img, atlas.image(), originX, originY, dw, dh, 0, 0, aw, ah, true);

    // 单位网格
    setcolor(theme::kTextFaint, img);
    setlinewidth(1.0f, img);
    for (int ux = 0; ux <= aw / uw; ++ux)
    {
        line(originX + ux * uw * kScale, originY, originX + ux * uw * kScale, originY + dh, img);
    }
    for (int uy = 0; uy <= ah / uw; ++uy)
    {
        line(originX, originY + uy * uw * kScale, originX + dw, originY + uy * uw * kScale, img);
    }

    // 单元标号
    text::setFont(theme::kFontTiny, false, img);
    for (int uy = 0; uy < ah / uw; ++uy)
    {
        for (int ux = 0; ux < aw / uw; ++ux)
        {
            char cell[64];
            std::snprintf(cell, sizeof(cell), "c%d r%d", ux, uy);
            setcolor(theme::kTextDim, img);
            text::draw(cell, originX + ux * uw * kScale + 4, originY + uy * uw * kScale + 3, img);

            std::snprintf(cell, sizeof(cell), "%d,%d", ux * uw, uy * uw);
            setcolor(theme::kTextFaint, img);
            text::draw(cell, originX + ux * uw * kScale + 4, originY + uy * uw * kScale + 18, img);
        }
    }

    // 精灵矩形 + 名字
    for (const auto& n : atlas.names())
    {
        const SpriteRect r = atlas.rect(n);
        const int rx = originX + r.x * kScale;
        const int ry = originY + r.y * kScale;
        const int rw = r.w * kScale;
        const int rh = r.h * kScale;

        setcolor(theme::kWarn, img);
        setlinewidth(2.0f, img);
        rectangle(rx, ry, rx + rw - 1, ry + rh - 1, img);
        setlinewidth(1.0f, img);

        text::setFont(theme::kFontSmall, true, img);
        setcolor(EGERGB(0xFF, 0xD9, 0x8A), img);
        text::draw(n.c_str(), rx + 5, ry + 5, img);
    }
}

void AtlasScene::draw(App& app, PIMAGE img)
{
    if (showCalibration_)
    {
        drawCalibration(app, img);
    }
    else
    {
        drawGallery(app, img);
    }
}

bool AtlasScene::onMouse(App& app, const mouse_msg& m)
{
    (void)app;
    (void)m;
    return false;
}

bool AtlasScene::onKey(App&, const key_msg& k)
{
    if (k.key == key_f1 || k.key == key_tab || k.key == key_space)
    {
        showCalibration_ = !showCalibration_;
        return true;
    }
    return false;
}

std::string AtlasScene::topBarInfo() const
{
    const Atlas& atlas = App::inst().atlas();
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "%s  %dx%d  unit=%d  sprites=%d   F1/Tab 切换视图",
                  showCalibration_ ? "校准层" : "画廊",
                  atlas.width(), atlas.height(), Atlas::kUnitPixels,
                  static_cast<int>(atlas.names().size()));
    return buf;
}

} // namespace chess
