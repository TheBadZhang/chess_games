#pragma once

#include <graphics.h>

#include <string>

namespace chess {

class App;

// ---------------------------------------------------------------------------
//  场景
// ---------------------------------------------------------------------------
//  App 内部维护一个场景栈：菜单在最底层，进入对局 / 工具时压栈，返回时弹栈。
//  栈顶场景独占输入与绘制。
// ---------------------------------------------------------------------------
class Scene
{
public:
    virtual ~Scene() = default;

    virtual const char* name() const = 0;

    // 生命周期。栈顶变化时由 App 调用。
    virtual void onEnter(App&) {}
    virtual void onExit(App&) {}

    // 每帧：先 update 再 draw。
    virtual void update(App&, double dtMs) { (void)dtMs; }

    // 绘制到整屏（App 已经画好顶栏与背景）
    virtual void draw(App&, PIMAGE img) = 0;

    // 输入分发。返回 true 表示已消费（App 不再处理）。
    virtual bool onMouse(App&, const mouse_msg&) { return false; }
    virtual bool onKey(App&, const key_msg&) { return false; }

    // 顶栏右侧的信息文本（可为空）
    virtual std::string topBarInfo() const { return {}; }

    // 离开本场景时是否需要保留 AI 线程（默认取消，避免后台空转）
    virtual bool keepAiAlive() const { return false; }

    // 后台分析是否已就绪。非对局场景默认就绪；
    // 对局场景在 AI 搜索完成前返回 false（离屏截图会据此等待）。
    virtual bool isAnalysisSettled() const { return true; }
};

} // namespace chess
