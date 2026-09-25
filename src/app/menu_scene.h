#pragma once

#include "app/scene.h"
#include "games/game.h"
#include "games/registry.h"

#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  主菜单布局常量
// ---------------------------------------------------------------------------
//  放在头文件里而不是 .cc 的匿名 namespace，是为了让布局自检（--ui-layout）
//  能用**同一份**数值去校验“字号调大后文本还放得下吗”。
//  如果自检抄一份副本，两边一旦不同步就失去了意义。
namespace menu_layout {

constexpr int kCardW   = 440;
// 卡片高度要容得下：游戏名(kFontBig=26) + 至多两行说明(kFontSmall=19) + 变体标签
constexpr int kCardH   = 168;
constexpr int kCardGap = 16;
constexpr int kCols    = 2;
constexpr int kPanelW  = 336;
constexpr int kPad     = 16;

// 卡片内部留白与文本区（与 drawCards 里的实参保持一致）
constexpr int kCardTextPad  = 20;   // 左右内边距
constexpr int kCardNameY    = 14;   // 游戏名相对卡片顶部
constexpr int kCardBlurbY   = 56;   // 说明文字相对卡片顶部
constexpr int kCardBlurbMax = 62;   // 说明文字可用高度（kFontSmall 两行）
constexpr int kCardTagGap   = 28;   // 变体标签距卡片底部

// 右侧配置面板
constexpr int kPanelBlurbY   = 42;   // 游戏说明文字相对内容区顶部
constexpr int kPanelBlurbMax = 60;   // 游戏说明可用高度（kFontSmall 两行）

// chip 组的统一间距：把 vertical 尺寸集中在这里，字号改了只需改这几个数
constexpr int kChipH        = 38;   // chip 高度
constexpr int kChipGapY     = 8;    // 同一组内换行的行距
constexpr int kGroupSubH    = 28;   // 组上方小标题（“变体/等级/…”）占位
constexpr int kGroupGapY    = 12;   // 组与组之间的间隔
constexpr int kLevelDetailH = 56;   // “等级说明”占位（kFontTiny 两行）

// 面板里第一组 chip 的起始 y（说明文字之后）
constexpr int kPanelChipTop = kPanelBlurbY + kPanelBlurbMax + 14;

// 工具面板
constexpr int kToolBlurbY   = 52;   // 工具说明相对内容区顶部
constexpr int kToolBlurbMax = 96;   // 工具说明可用高度

} // namespace menu_layout

// ---------------------------------------------------------------------------
//  主菜单
// ---------------------------------------------------------------------------
//  左侧：游戏卡片网格（末位接一组"工具"卡片）
//  右侧：选中项的配置面板 —— 变体 / 棋盘尺寸 / AI 等级 / 执子 / 规则开关 / 开始
//
//  交互用即时模式：每帧重建一次布局（rebuildLayout），把可点击区域记进 hits_，
//  输入处理只查这张表。布局与绘制共用同一份矩形，不会出现"看到的和点到的不一致"。
// ---------------------------------------------------------------------------
class MenuScene : public Scene
{
public:
    const char* name() const override { return "主菜单"; }

    void onEnter(App& app) override;
    void update(App& app, double dtMs) override;
    void draw(App& app, PIMAGE img) override;
    bool onMouse(App& app, const mouse_msg& m) override;
    bool onKey(App& app, const key_msg& k) override;
    std::string topBarInfo() const override;

    // 仅供 --ui-layout 自检使用。
    // 用真实的面板内框算出“最后一组 chip 的底部 y”，走的是和 rebuildLayout
    // 完全相同的代码路径 —— 这样调字号/间距时自检不会与实际布局脱节。
    static int optionsContentBottom(const Rect& inner, const GameDesc& d);

private:
    enum class EntryKind
    {
        Game = 0,
        Tool,
    };

    struct Entry
    {
        EntryKind kind  = EntryKind::Game;
        int       index = 0;   // 在 allGames() / allTools() 中的下标
    };

    enum class HitKind
    {
        Card = 0,
        Variant,
        Board,
        Level,
        Side,
        RuleToggle,
        Handicap,
        Start,
        ResetConfig,
    };

    struct Hit
    {
        Rect    r;
        HitKind kind  = HitKind::Card;
        int     value = 0;
    };

    void rebuildLayout(App& app);
    void selectEntry(App& app, int entryIndex);
    void drawCards(App& app, PIMAGE img);
    void drawOptions(App& app, PIMAGE img);

    // 把一组 chip 按行流式排布到 area 内，并把可点击区域追加到 hits。
    // 返回排完后的下一行 y（作为私有静态成员，才能访问私有嵌套类型 Hit）。
    static int layoutChips(std::vector<Hit>& hits, HitKind kind, const Rect& area, int y,
                           const std::vector<std::string>& labels);

    // 把“变体 / 尺寸 / 等级 / 等级说明 / 执子 / 规则 / 让子”依次排下来。
    // 返回最后一组之后的光标 y。为了复用，hits 允许传一个丢弃用的临时容器。
    static int layoutGroups(std::vector<Hit>& hits, const Rect& inner, int topY,
                            const GameDesc& d);

    const GameDesc* currentGameDesc() const;

    std::vector<Entry> entries_;
    std::vector<Hit>   hits_;
    std::vector<Rect>  cardRects_;

    int    selected_     = 0;
    int    hoverHit_     = -1;
    double blinkMs_      = 0.0;
    double finishedMs_   = 0.0;   // 最近一局结束后的高亮动画（预留）
};

} // namespace chess
