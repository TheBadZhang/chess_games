#include "app/app.h"

#include "core/config.h"
#include "core/stopwatch.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <cstdio>

namespace chess {

App& App::inst()
{
    static App app;
    return app;
}

bool App::init(std::string* err)
{
    if (!atlas_.load("res/chesses100.png", "chesses.txt", err))
    {
        return false;
    }
    // 字体层默认值：中文字形按矩形居中时会略微偏下，统一向上偏 1px
    text::setVerticalBias(-1);
    return true;
}

Rect App::screen() const
{
    return Rect{0, 0, kWindowWidth, kWindowHeight};
}

Rect App::content() const
{
    return Rect{0, theme::kTopBarH, kWindowWidth, kWindowHeight - theme::kTopBarH};
}

Scene* App::current()
{
    return stack_.empty() ? nullptr : stack_.back().get();
}

void App::pushScene(std::unique_ptr<Scene> s)
{
    pendingPush_ = std::move(s);
}

void App::replaceScene(std::unique_ptr<Scene> s)
{
    pendingPush_ = std::move(s);
    pendingPop_  = true;
}

void App::popScene()
{
    pendingPop_ = true;
}

// 场景切换统一延迟到帧边界处理：
// 否则会在 Scene 自己的成员函数里把 Scene 销毁掉。
void App::applyPendingScene()
{
    if (pendingPush_)
    {
        if (Scene* cur = current())
        {
            cur->onExit(*this);
            if (!cur->keepAiAlive())
            {
                ai_.cancelAndJoin();
            }
        }
        stack_.push_back(std::move(pendingPush_));
        pendingPush_.reset();
        if (Scene* now = current())
        {
            now->onEnter(*this);
        }
    }

    if (pendingPop_)
    {
        pendingPop_ = false;
        if (!stack_.empty())
        {
            Scene* top = stack_.back().get();
            top->onExit(*this);
            if (!top->keepAiAlive())
            {
                ai_.cancelAndJoin();
            }
            stack_.pop_back();
            if (Scene* now = current())
            {
                now->onEnter(*this);
            }
        }
    }
}

void App::toast(const std::string& msg, double ms)
{
    toasts_.push_back(ActiveToast{msg, ms});
    if (toasts_.size() > 4)
    {
        toasts_.erase(toasts_.begin());
    }
}

// ---------------------------------------------------------------------------
//  顶栏
// ---------------------------------------------------------------------------

namespace {

// 返回按钮位置（只有栈深 > 1 时才画）
Rect backButtonRect()
{
    return Rect{theme::kPad, 12, 116, theme::kTopBarH - 24};
}

} // namespace

void App::drawTopBar(PIMAGE img)
{
    const Rect bar{0, 0, kWindowWidth, theme::kTopBarH};
    ui::fillBox(img, bar, theme::kPanel);
    ui::fillBox(img, Rect{0, theme::kTopBarH - 1, kWindowWidth, 1}, theme::kLine);

    int textX = theme::kPad;

    // 返回按钮
    if (stack_.size() > 1)
    {
        const Rect br = backButtonRect();
        const bool hovered = br.contains(mouseX_, mouseY_);
        ui::button(img, br, "返回", hovered ? ui::State::Hover : ui::State::Normal);
        textX = br.right() + theme::kPad;
    }

    // 标题
    text::setFont(theme::kFontBig, true, img);
    setcolor(theme::kText, img);
    text::draw("棋类游戏合集", textX, 14, img);

    // 场景名
    if (Scene* s = const_cast<App*>(this)->current())
    {
        text::setFont(theme::kFontSmall, false, img);
        setcolor(theme::kTextFaint, img);
        text::draw(s->name(), textX + text::width("棋类游戏合集", img) + 14, 20, img);

        // 右侧信息
        const std::string info = s->topBarInfo();
        if (!info.empty())
        {
            setcolor(theme::kTextDim, img);
            text::drawIn(info.c_str(), Rect{0, 0, kWindowWidth - theme::kPad, theme::kTopBarH},
                         Align::Right, VAlign::Middle, img);
        }
    }
}

// ---------------------------------------------------------------------------
//  Toast
// ---------------------------------------------------------------------------

void App::drawToasts(PIMAGE img, double dt)
{
    if (toasts_.empty())
    {
        return;
    }

    text::setFont(theme::kFontSmall, false, img);

    int y = kWindowHeight - theme::kPad - 40;
    // 从最新往上画
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it)
    {
        const double alpha = it->remainMs < 400.0 ? it->remainMs / 400.0 : 1.0;
        (void)alpha;

        const int tw = text::width(it->text.c_str(), img);
        const Rect r{kWindowWidth - theme::kPad - tw - 28, y, tw + 28, 36};

        ui::fillRound(img, r, theme::kRadiusSm, theme::kPanelHi);
        ui::strokeRound(img, r, theme::kRadiusSm, theme::kLine, 1);
        setcolor(theme::kText, img);
        text::drawIn(it->text.c_str(), r, Align::Center, VAlign::Middle, img);

        y -= 44;
    }

    for (auto& t : toasts_)
    {
        t.remainMs -= dt;
    }
    while (!toasts_.empty() && toasts_.front().remainMs <= 0.0)
    {
        toasts_.erase(toasts_.begin());
    }
}

// ---------------------------------------------------------------------------
//  主循环
// ---------------------------------------------------------------------------

void App::renderFrame(PIMAGE img, double dt)
{
    setbkcolor(theme::kBg, img);
    cleardevice(img);

    if (Scene* s = current())
    {
        s->draw(*this, img);
    }
    drawTopBar(img);
    drawToasts(img, dt);
}

void App::dispatchSyntheticKey(const key_msg& k)
{
    // 先处理场景切换，保证 current() 指向刚 push 进来的场景
    applyPendingScene();
    if (Scene* s = current())
    {
        s->onKey(*this, k);
    }
    applyPendingScene();
}

void App::updateForShot(double dtMs)
{
    applyPendingScene();
    if (Scene* s = current())
    {
        s->update(*this, dtMs);
    }
}

bool App::sceneAnalysisSettled() const
{
    const Scene* s = stack_.empty() ? nullptr : stack_.back().get();
    return !s || s->isAnalysisSettled();
}

void App::run()
{
    FrameClock clock;

    while (running_ && is_run())
    {
        const double dt = clock.tick();
        dtMs_ = dt;
        fps_  = clock.fps();

        // ---- 输入 ----
        while (mousemsg())
        {
            const mouse_msg m = getmouse();
            mouseX_ = m.x;
            mouseY_ = m.y;

            if (m.is_down())
            {
                mouseDown_ = true;
            }
            else if (m.is_up())
            {
                mouseDown_ = false;
            }

            // 顶栏的返回按钮优先
            if (m.is_down() && m.is_left() && stack_.size() > 1 &&
                backButtonRect().contains(m.x, m.y))
            {
                popScene();
                continue;
            }

            if (Scene* s = current())
            {
                s->onMouse(*this, m);
            }
        }

        while (kbmsg())
        {
            const key_msg k = getkey();
            if (k.msg != key_msg_down)
            {
                continue;
            }

            if (k.key == key_esc && stack_.size() <= 1)
            {
                running_ = false;
                break;
            }

            if (Scene* s = current())
            {
                s->onKey(*this, k);
            }
        }

        // ---- 逻辑 ----
        if (Scene* s = current())
        {
            s->update(*this, dt);
        }

        // ---- 绘制 ----
        renderFrame(nullptr, dt);

        // ---- 场景切换（帧边界）----
        applyPendingScene();

        delay_fps(60);
    }

    ai_.cancelAndJoin();
}

} // namespace chess
