#pragma once

#include "app/ai_worker.h"
#include "app/scene.h"
#include "core/types.h"
#include "games/game.h"
#include "ui/atlas.h"

#include <memory>
#include <string>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  应用外壳
// ---------------------------------------------------------------------------
//  职责：
//    * 场景栈（菜单 <-> 对局 / 工具）
//    * 输入轮询与分发、鼠标状态
//    * 共享资源（图集、AI 工作线程、上次对局配置）
//    * 顶栏（标题 / 返回按钮 / FPS）与 toast
//
//  主循环里 EGE 的 initgraph / closegraph 由 main.cc 负责，
//  App::run() 只跑循环体。
// ---------------------------------------------------------------------------
class App
{
public:
    static App& inst();

    // 加载图集等共享资源。失败时把原因写入 err。
    bool init(std::string* err);

    void run();

    // ---- 场景 ----
    void pushScene(std::unique_ptr<Scene> s);
    void replaceScene(std::unique_ptr<Scene> s);
    void popScene();
    void quit() { running_ = false; }

    // ---- 共享资源 ----
    Atlas&      atlas() { return atlas_; }
    AiWorker&   ai() { return ai_; }
    GameConfig& config() { return config_; }

    // ---- 画面区域 ----
    Rect screen() const;
    Rect content() const;   // 去掉顶栏后的可用区域

    // ---- 鼠标 ----
    int  mouseX() const { return mouseX_; }
    int  mouseY() const { return mouseY_; }
    bool mouseDown() const { return mouseDown_; }

    // ---- 提示条 ----
    void toast(const std::string& msg, double ms = 2200);

    // ---- 帧信息 ----
    double dtMs() const { return dtMs_; }
    double fps() const { return fps_; }

    // ---- 离屏渲染（供主循环与自动化截图共用）----
    // 渲染一帧到指定目标（nullptr = 窗口）。包含场景 + 顶栏 + toast。
    void renderFrame(PIMAGE img, double dtMs);
    // 设置虚拟鼠标位置：离屏截图时用来呈现 hover / 选中态
    void setVirtualMouse(int x, int y)
    {
        mouseX_ = x;
        mouseY_ = y;
    }
    // 走与真实输入相同的分发路径（离屏截图用）
    void dispatchSyntheticKey(const key_msg& k);
    void updateForShot(double dtMs);

    // 当前场景的后台分析是否已就绪（离屏截图时用来等结果）
    bool sceneAnalysisSettled() const;

private:
    App() = default;

    Scene* current();
    void   applyPendingScene();
    void   drawTopBar(PIMAGE img);
    void   drawToasts(PIMAGE img, double dtMs);

    std::vector<std::unique_ptr<Scene>> stack_;
    std::unique_ptr<Scene>              pendingPush_;
    bool                                pendingPop_ = false;

    Atlas     atlas_;
    AiWorker  ai_;
    GameConfig config_{};

    int    mouseX_    = -1;
    int    mouseY_    = -1;
    bool   mouseDown_ = false;
    bool   running_   = true;
    double dtMs_      = 0.0;
    double fps_       = 0.0;

    struct ActiveToast
    {
        std::string text;
        double      remainMs = 0.0;
    };
    std::vector<ActiveToast> toasts_;
};

// 便捷入口：注册表里按 id 创建游戏场景（菜单点击游戏卡片时用）
std::unique_ptr<Scene> makeGameScene(const GameDesc& desc, GameConfig cfg);

} // namespace chess
