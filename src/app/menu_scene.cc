#include "app/menu_scene.h"

#include "app/app.h"
#include "app/atlas_scene.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>

namespace chess {

using namespace menu_layout;

namespace {

// 测量文本宽度（需要先设好字体，否则量到的是上一次的字体）
int measure(const char* s, int px, bool bold)
{
    text::setFont(px, bold, nullptr);
    return text::width(s, nullptr);
}

} // namespace

// 把一组 chip 按行流式排布到 area 内
int MenuScene::layoutChips(std::vector<Hit>& hits, HitKind kind, const Rect& area, int y,
                           const std::vector<std::string>& labels)
{
    constexpr int kGapX  = 8;

    int x = area.x;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const int w = std::max(52, measure(labels[i].c_str(), theme::kFontTiny, false) + 26);
        if (x + w > area.x + area.w && x > area.x)
        {
            x = area.x;
            y += kChipH + kChipGapY;
        }

        const Rect r{x, y, w, kChipH};
        hits.push_back(Hit{r, kind, static_cast<int>(i)});

        x += w + kGapX;
    }
    return y + kChipH;
}

// ---------------------------------------------------------------------------
//  生命周期
// ---------------------------------------------------------------------------

void MenuScene::onEnter(App& app)
{
    entries_.clear();
    for (size_t i = 0; i < allGames().size(); ++i)
    {
        entries_.push_back(Entry{EntryKind::Game, static_cast<int>(i)});
    }
    for (size_t i = 0; i < allTools().size(); ++i)
    {
        entries_.push_back(Entry{EntryKind::Tool, static_cast<int>(i)});
    }

    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size()))
    {
        selected_ = 0;
    }

    app.toast("选择一项后点「开始」");
}

void MenuScene::update(App& app, double dtMs)
{
    blinkMs_ += dtMs;
    rebuildLayout(app);

    // hover 命中
    hoverHit_ = -1;
    for (size_t i = 0; i < hits_.size(); ++i)
    {
        if (hits_[i].r.contains(app.mouseX(), app.mouseY()))
        {
            hoverHit_ = static_cast<int>(i);
            break;
        }
    }
}

const GameDesc* MenuScene::currentGameDesc() const
{
    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size()))
    {
        return nullptr;
    }
    const Entry& e = entries_[selected_];
    if (e.kind != EntryKind::Game)
    {
        return nullptr;
    }
    const auto& games = allGames();
    if (e.index < 0 || e.index >= static_cast<int>(games.size()))
    {
        return nullptr;
    }
    return &games[e.index];
}

void MenuScene::selectEntry(App& app, int entryIndex)
{
    if (entryIndex == selected_ || entryIndex < 0 ||
        entryIndex >= static_cast<int>(entries_.size()))
    {
        return;
    }
    selected_ = entryIndex;

    // 切到另一个游戏时，把配置重置为该游戏的第一组默认值，
    // 否则会把上一个游戏的棋盘尺寸/变体索引带过来
    if (const GameDesc* d = currentGameDesc())
    {
        GameConfig& cfg = app.config();
        cfg.variant   = 0;
        cfg.aiLevel   = 3;
        cfg.humanSide = 1;
        cfg.handicap  = 0;
        cfg.ruleOption = false;
        cfg.humanVsHuman = false;
        if (!d->boardPresets.empty())
        {
            cfg.cols = d->boardPresets[0].cols;
            cfg.rows = d->boardPresets[0].rows;
        }
    }
}

// ---------------------------------------------------------------------------
//  布局
// ---------------------------------------------------------------------------

void MenuScene::rebuildLayout(App& app)
{
    hits_.clear();
    cardRects_.clear();

    const Rect content = app.content();
    const int  leftX   = content.x + kPad;
    const int  leftW   = content.w - kPanelW - 3 * kPad;

    // 卡片网格
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const int col = static_cast<int>(i) % kCols;
        const int row = static_cast<int>(i) / kCols;
        const Rect r{leftX + col * (kCardW + kCardGap),
                     content.y + kPad + row * (kCardH + kCardGap),
                     kCardW, kCardH};
        cardRects_.push_back(r);
        hits_.push_back(Hit{r, HitKind::Card, static_cast<int>(i)});
    }

    // 右侧面板
    const Rect panel{content.right() - kPanelW - kPad, content.y + kPad,
                     kPanelW, content.h - 2 * kPad};
    const Rect inner = panel.inset(kPad);

    if (const GameDesc* d = currentGameDesc())
    {
        layoutGroups(hits_, inner, inner.y + kPanelChipTop, *d);

        // ---- 开始按钮（贴底）----
        const Rect start{inner.x, panel.bottom() - kPad - 48, inner.w, 48};
        hits_.push_back(Hit{start, HitKind::Start, 0});

        const Rect reset{inner.x, start.y - 42, inner.w, 34};
        hits_.push_back(Hit{reset, HitKind::ResetConfig, 0});
    }
    else
    {
        const Rect start{inner.x, panel.bottom() - kPad - 48, inner.w, 48};
        hits_.push_back(Hit{start, HitKind::Start, 0});
    }
}

int MenuScene::layoutGroups(std::vector<Hit>& hits, const Rect& inner, int topY,
                            const GameDesc& d)
{
    // 游标式布局：从"简介文字之后"开始往下排。
    // 每个组先留出小标题的高度，再放 chip；layoutChips 的返回值直接就是
    // 本组最后一行的底部，所以不需要再手算 30/26/40 这类魔法数 ——
    // 调字号时只改 menu_layout 里的那几个常量就够了。
    int y = topY;

    auto nextGroup = [&](HitKind kind, const std::vector<std::string>& labels) {
        y += kGroupSubH;
        const int bottom = layoutChips(hits, kind, Rect{inner.x, y, inner.w, inner.h},
                                       y, labels);
        y = bottom + kGroupGapY;
    };

    // ---- 变体 ----
    if (!d.variants.empty())
    {
        nextGroup(HitKind::Variant, d.variants);
    }

    // ---- 棋盘尺寸 ----
    if (d.boardPresets.size() > 1)
    {
        std::vector<std::string> labels;
        for (const auto& p : d.boardPresets)
        {
            labels.push_back(p.label);
        }
        nextGroup(HitKind::Board, labels);
    }

    // ---- AI 等级 ----
    const std::vector<LevelDesc>& lv = d.levels.empty() ? defaultLevels() : d.levels;
    {
        std::vector<std::string> labels;
        for (size_t i = 0; i < lv.size(); ++i)
        {
            labels.push_back(lv[i].name);
        }
        nextGroup(HitKind::Level, labels);
    }

    // 等级说明文字（drawOptions 会画在这里，提前把高度挖出来）
    y += kLevelDetailH;

    // ---- 执子 ----
    nextGroup(HitKind::Side, {"人执先手", "人执后手", "双人对下"});

    // ---- 规则开关 ----
    if (d.ruleOptionName)
    {
        y += kGroupSubH;
        text::setFont(theme::kFontTiny, false, nullptr);
        const int  w = std::max(96, text::width(d.ruleOptionName, nullptr) + 30);
        const Rect r{inner.x, y, w, kChipH};
        hits.push_back(Hit{r, HitKind::RuleToggle, 0});
        y = r.bottom() + kGroupGapY;
    }

    // ---- 让子 ----
    if (d.handicapName)
    {
        std::vector<std::string> labels;
        for (int i = 0; i <= 9; i += 3)
        {
            labels.push_back(std::to_string(i));
        }
        nextGroup(HitKind::Handicap, labels);
    }

    return y;
}

int MenuScene::optionsContentBottom(const Rect& inner, const GameDesc& d)
{
    std::vector<Hit> discard;
    return layoutGroups(discard, inner, inner.y + kPanelChipTop, d);
}

// ---------------------------------------------------------------------------
//  绘制
// ---------------------------------------------------------------------------

void MenuScene::drawCards(App& app, PIMAGE img)
{
    const auto& games = allGames();
    const auto& tools = allTools();

    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const Entry& e = entries_[i];
        const Rect&  r = cardRects_[i];

        const bool sel = (static_cast<int>(i) == selected_);
        bool hovered = false;
        for (const Hit& h : hits_)
        {
            if (h.kind == HitKind::Card && h.value == static_cast<int>(i) &&
                h.r.contains(app.mouseX(), app.mouseY()))
            {
                hovered = true;
            }
        }

        const color_t border = sel ? theme::kAccent : (hovered ? theme::kAccentDim : theme::kLine);
        ui::panel(img, r, sel ? theme::kPanelHi : theme::kPanel, border, theme::kRadius);

        if (sel)
        {
            // 左侧强调条，让"当前选中"在深色背景里一眼可见
            ui::fillRound(img, Rect{r.x, r.y + 12, 4, r.h - 24}, 2, theme::kAccent);
        }

        const char* name  = "";
        const char* blurb = "";
        std::string tag;

        if (e.kind == EntryKind::Game)
        {
            const GameDesc& d = games[e.index];
            name  = d.name;
            blurb = d.blurb;
            if (!d.variants.empty())
            {
                tag = "变体: " + d.variants[0];
            }
        }
        else
        {
            const ToolDesc& t = tools[e.index];
            name  = t.name;
            blurb = t.blurb;
            tag   = "工具";
        }

        text::setFont(theme::kFontBig, true, img);
        setcolor(theme::kText, img);
        text::draw(name, r.x + kCardTextPad, r.y + kCardNameY, img);

        // 说明文字改用自动换行：字号调大后固定单行会溢出卡片宽度。
        // 可用高度与 --ui-layout 自检里用的常量是同一个。
        text::setFont(theme::kFontSmall, false, img);
        setcolor(theme::kTextDim, img);
        text::drawWrapped(blurb,
                          Rect{r.x + kCardTextPad, r.y + kCardBlurbY,
                               r.w - 2 * kCardTextPad, kCardBlurbMax},
                          Align::Left, 0, img);

        if (!tag.empty())
        {
            text::setFont(theme::kFontTiny, false, img);
            setcolor(theme::kTextFaint, img);
            text::draw(tag.c_str(), r.x + kCardTextPad, r.bottom() - kCardTagGap, img);
        }
    }
}

void MenuScene::drawOptions(App& app, PIMAGE img)
{
    const Rect content = app.content();
    const Rect panel{content.right() - kPanelW - kPad, content.y + kPad,
                     kPanelW, content.h - 2 * kPad};
    const Rect inner = panel.inset(kPad);

    ui::panel(img, panel, theme::kPanel, theme::kLine, theme::kRadius);

    if (const GameDesc* d = currentGameDesc())
    {
        GameConfig& cfg = app.config();

        int y = inner.y + 4;

    text::setFont(theme::kFontBig, true, img);
    setcolor(theme::kText, img);
    text::draw(d->name, inner.x, y, img);
    y += 32;

    text::setFont(theme::kFontSmall, false, img);
    setcolor(theme::kTextDim, img);
    text::drawWrapped(d->blurb, Rect{inner.x, y, inner.w, kPanelBlurbMax},
                      Align::Left, 0, img);

    // 收集各类 chip 的绘制目标，按 hits_ 顺序绘制
    const std::vector<LevelDesc>& lv = d->levels.empty() ? defaultLevels() : d->levels;

    auto chipRect = [&](HitKind kind, int value, Rect* out) {
        for (const Hit& h : hits_)
        {
            if (h.kind == kind && h.value == value)
            {
                *out = h.r;
                return true;
            }
        }
        return false;
    };

    auto hoveredIs = [&](HitKind kind, int value) {
        if (hoverHit_ < 0)
        {
            return false;
        }
        const Hit& h = hits_[hoverHit_];
        return h.kind == kind && h.value == value;
    };

    // ---- 变体 ----
    if (!d->variants.empty())
    {
        text::setFont(theme::kFontTiny, true, img);
        setcolor(theme::kTextFaint, img);
        // 取该组第一个 chip 的 y 作为小标题位置
        Rect first{};
        if (chipRect(HitKind::Variant, 0, &first))
        {
            text::draw("变体", first.x, first.y - 20, img);
        }
        for (size_t i = 0; i < d->variants.size(); ++i)
        {
            Rect r{};
            if (chipRect(HitKind::Variant, static_cast<int>(i), &r))
            {
                const bool on = (cfg.variant == static_cast<int>(i));
                ui::chip(img, r, d->variants[i].c_str(), on);
                if (!on && hoveredIs(HitKind::Variant, static_cast<int>(i)))
                {
                    ui::strokeRound(img, r, r.h / 2, theme::kAccentDim, 1);
                }
            }
        }
    }

    // ---- 棋盘尺寸 ----
    if (d->boardPresets.size() > 1)
    {
        Rect first{};
        text::setFont(theme::kFontTiny, true, img);
        setcolor(theme::kTextFaint, img);
        if (chipRect(HitKind::Board, 0, &first))
        {
            text::draw("棋盘", first.x, first.y - 20, img);
        }
        for (size_t i = 0; i < d->boardPresets.size(); ++i)
        {
            Rect r{};
            if (chipRect(HitKind::Board, static_cast<int>(i), &r))
            {
                const bool on = (cfg.cols == d->boardPresets[i].cols &&
                                 cfg.rows == d->boardPresets[i].rows);
                ui::chip(img, r, d->boardPresets[i].label, on);
                if (!on && hoveredIs(HitKind::Board, static_cast<int>(i)))
                {
                    ui::strokeRound(img, r, r.h / 2, theme::kAccentDim, 1);
                }
            }
        }
    }

    // ---- AI 等级 ----
    {
        Rect first{};
        text::setFont(theme::kFontTiny, true, img);
        setcolor(theme::kTextFaint, img);
        if (chipRect(HitKind::Level, 0, &first))
        {
            text::draw("AI 等级", first.x, first.y - 20, img);
        }
        for (size_t i = 0; i < lv.size(); ++i)
        {
            Rect r{};
            if (chipRect(HitKind::Level, static_cast<int>(i), &r))
            {
                const bool on = (cfg.aiLevel == static_cast<int>(i) + 1);
                ui::chip(img, r, lv[i].name, on);
                if (!on && hoveredIs(HitKind::Level, static_cast<int>(i)))
                {
                    ui::strokeRound(img, r, r.h / 2, theme::kAccentDim, 1);
                }
            }
        }

        // 等级说明
        const int idx = std::clamp(cfg.aiLevel - 1, 0, static_cast<int>(lv.size()) - 1);
        Rect levelRow{};
        if (chipRect(HitKind::Level, 0, &levelRow))
        {
            text::setFont(theme::kFontTiny, false, img);
            setcolor(theme::kTextDim, img);
            text::drawWrapped(lv[idx].detail,
                              Rect{inner.x, levelRow.bottom() + 8, inner.w, kLevelDetailH},
                              Align::Left, 0, img);
        }
    }

    // ---- 执子 ----
    {
        Rect first{};
        text::setFont(theme::kFontTiny, true, img);
        setcolor(theme::kTextFaint, img);
        if (chipRect(HitKind::Side, 0, &first))
        {
            text::draw("执子", first.x, first.y - 20, img);
        }
        const char* labels[3] = {"人执先手", "人执后手", "双人对下"};
        for (int i = 0; i < 3; ++i)
        {
            Rect r{};
            if (chipRect(HitKind::Side, i, &r))
            {
                const bool on = cfg.humanVsHuman ? (i == 2) : (cfg.humanSide == i + 1);
                ui::chip(img, r, labels[i], on);
                if (!on && hoveredIs(HitKind::Side, i))
                {
                    ui::strokeRound(img, r, r.h / 2, theme::kAccentDim, 1);
                }
            }
        }
    }

    // ---- 规则开关 ----
    if (d->ruleOptionName)
    {
        Rect r{};
        if (chipRect(HitKind::RuleToggle, 0, &r))
        {
            ui::chip(img, r, d->ruleOptionName, cfg.ruleOption);
            if (!cfg.ruleOption && hoveredIs(HitKind::RuleToggle, 0))
            {
                ui::strokeRound(img, r, r.h / 2, theme::kAccentDim, 1);
            }
        }
    }

    // ---- 让子 ----
    if (d->handicapName)
    {
        Rect first{};
        text::setFont(theme::kFontTiny, true, img);
        setcolor(theme::kTextFaint, img);
        if (chipRect(HitKind::Handicap, 0, &first))
        {
            text::draw(d->handicapName, first.x, first.y - 20, img);
        }
        for (int i = 0; i <= 9; i += 3)
        {
            Rect r{};
            if (chipRect(HitKind::Handicap, i / 3, &r))
            {
                const bool on = (cfg.handicap == i);
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), "%d", i);
                ui::chip(img, r, lbl, on);
            }
        }
    }

    // ---- 底部按钮 ----
    for (const Hit& h : hits_)
    {
        if (h.kind == HitKind::ResetConfig)
        {
            const bool hov = (hoverHit_ >= 0 && hits_[hoverHit_].kind == HitKind::ResetConfig);
            ui::button(img, h.r, "恢复默认配置", hov ? ui::State::Hover : ui::State::Normal);
        }
        else if (h.kind == HitKind::Start)
        {
            const bool hov = (hoverHit_ >= 0 && hits_[hoverHit_].kind == HitKind::Start);
            ui::button(img, h.r, "开始", hov ? ui::State::Hover : ui::State::Selected);
        }
    }
    }
    else
    {
        // ---- 工具项面板 ----
        const Entry&    e = entries_[selected_];
        const ToolDesc& t = allTools()[e.index];

        text::setFont(theme::kFontBig, true, img);
        setcolor(theme::kText, img);
        text::draw(t.name, inner.x, inner.y + 4, img);

        text::setFont(theme::kFontSmall, false, img);
        setcolor(theme::kTextDim, img);
        text::drawWrapped(t.blurb,
                          Rect{inner.x, inner.y + kToolBlurbY, inner.w, kToolBlurbMax},
                          Align::Left, 0, img);

        for (const Hit& h : hits_)
        {
            if (h.kind == HitKind::Start)
            {
                const bool hov = (hoverHit_ >= 0 && hits_[hoverHit_].kind == HitKind::Start);
                ui::button(img, h.r, "打开", hov ? ui::State::Hover : ui::State::Selected);
            }
        }
    }
}

void MenuScene::draw(App& app, PIMAGE img)
{
    drawCards(app, img);
    drawOptions(app, img);
}

// ---------------------------------------------------------------------------
//  输入
// ---------------------------------------------------------------------------

bool MenuScene::onMouse(App& app, const mouse_msg& m)
{
    if (!m.is_down() || !m.is_left())
    {
        return false;
    }

    for (const Hit& h : hits_)
    {
        if (!h.r.contains(m.x, m.y))
        {
            continue;
        }

        GameConfig& cfg = app.config();

        switch (h.kind)
        {
        case HitKind::Card:
            selectEntry(app, h.value);
            return true;

        case HitKind::Variant:
            cfg.variant = h.value;
            return true;

        case HitKind::Level:
            cfg.aiLevel = h.value + 1;
            return true;

        case HitKind::RuleToggle:
            cfg.ruleOption = !cfg.ruleOption;
            return true;

        case HitKind::Board:
        case HitKind::Handicap:
        case HitKind::Side:
        case HitKind::ResetConfig:
        case HitKind::Start:
            break;
        }

        // 需要 desc 的分支
        const GameDesc* d = currentGameDesc();
        if (!d)
        {
            if (h.kind == HitKind::Start)
            {
                const Entry& e = entries_[selected_];
                if (e.kind == EntryKind::Tool && allTools()[e.index].id == std::string("atlas"))
                {
                    app.pushScene(std::make_unique<AtlasScene>());
                }
                return true;
            }
            continue;
        }

        switch (h.kind)
        {
        case HitKind::Board:
            if (h.value < static_cast<int>(d->boardPresets.size()))
            {
                cfg.cols = d->boardPresets[h.value].cols;
                cfg.rows = d->boardPresets[h.value].rows;
            }
            return true;

        case HitKind::Handicap:
            cfg.handicap = h.value * 3;
            return true;

        case HitKind::Side:
            cfg.humanVsHuman = (h.value == 2);
            cfg.humanSide    = (h.value == 1) ? 2 : 1;
            return true;

        case HitKind::ResetConfig:
            cfg.variant    = 0;
            cfg.aiLevel    = 3;
            cfg.humanSide  = 1;
            cfg.handicap   = 0;
            cfg.ruleOption = false;
            cfg.humanVsHuman = false;
            if (!d->boardPresets.empty())
            {
                cfg.cols = d->boardPresets[0].cols;
                cfg.rows = d->boardPresets[0].rows;
            }
            app.toast("已恢复默认配置");
            return true;

        case HitKind::Start:
            if (d->create)
            {
                app.pushScene(makeGameScene(*d, cfg));
            }
            else
            {
                app.toast("该游戏尚未实现");
            }
            return true;

        default:
            break;
        }
    }

    return false;
}

bool MenuScene::onKey(App& app, const key_msg& k)
{
    const int n = static_cast<int>(entries_.size());
    if (n == 0)
    {
        return false;
    }

    switch (k.key)
    {
    case key_left:
        selectEntry(app, (selected_ + n - 1) % n);
        return true;
    case key_right:
        selectEntry(app, (selected_ + 1) % n);
        return true;
    case key_up:
        selectEntry(app, (selected_ + n - kCols) % n);
        return true;
    case key_down:
        selectEntry(app, (selected_ + kCols) % n);
        return true;
    case key_enter:
        if (const GameDesc* d = currentGameDesc())
        {
            if (d->create)
            {
                app.pushScene(makeGameScene(*d, app.config()));
            }
            else
            {
                app.toast("该游戏尚未实现");
            }
        }
        return true;
    default:
        break;
    }
    return false;
}

std::string MenuScene::topBarInfo() const
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "共 %d 个游戏 / %d 个工具   方向键切换   Enter 开始",
                  static_cast<int>(allGames().size()),
                  static_cast<int>(allTools().size()));
    return buf;
}

} // namespace chess
