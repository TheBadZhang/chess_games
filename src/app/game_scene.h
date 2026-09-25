#pragma once

#include "ai/ai.h"
#include "app/scene.h"
#include "games/game.h"
#include "ui/board_view.h"

#include <memory>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  通用对局场景
// ---------------------------------------------------------------------------
//  完全由 IGame + IAI 驱动，四个游戏共用这一份实现。
//
//  "分析"与"走棋"是同一套机制：
//    局面一变就重启一次后台搜索（在 AI 工作线程里跑）。
//      * 轮到 AI 走   -> 搜索结果当作要走的那一手
//      * 轮到人走     -> 同一份结果当作提示（推荐着法 / 主变 / 胜率）
//    所以提示是"真分析"出来的，而不是另一套独立的启发式；
//    等级参数同时决定了 AI 的棋力与提示的信息量。
// ---------------------------------------------------------------------------
class GameScene : public Scene
{
public:
    GameScene(std::unique_ptr<IGame> game, std::unique_ptr<IAI> ai, GameConfig cfg);

    const char* name() const override;
    void onEnter(App& app) override;
    void onExit(App& app) override;
    void update(App& app, double dtMs) override;
    void draw(App& app, PIMAGE img) override;
    bool onMouse(App& app, const mouse_msg& m) override;
    bool onKey(App& app, const key_msg& k) override;
    std::string topBarInfo() const override;
    bool isAnalysisSettled() const override { return hasAnalysis_ || !ai_; }

private:
    enum class Btn
    {
        Undo = 0,
        Restart,
        ToggleHint,
        AiMove,
        Pass,
        Count,
    };

    // ---- 流程 ----
    bool isHumanTurn() const;
    bool needsAnalysis() const;
    void startAnalysis(App& app);
    void onAnalysisDone(App& app, const AiOutcome& out, int elapsedMs);
    void applyMove(App& app, const Move& m);
    void undoOne();
    void undoHumanTurn(App& app);
    void restart(App& app);

    // ---- 布局与绘制 ----
    void rebuildLayout(const App& app);
    void drawBoardArea(App& app, PIMAGE img);
    void drawHud(App& app, PIMAGE img);
    void drawHintOverlay(const App& app, PIMAGE img);

    // ---- 输入 ----
    void onCellClick(App& app, Coord c);

    std::unique_ptr<IGame> game_;
    std::unique_ptr<IAI>   ai_;
    GameConfig             cfg_;

    BoardView  bv_;
    Rect       boardArea_{};
    Rect       hudRect_{};
    Rect       btnRects_[static_cast<int>(Btn::Count)]{};

    // 分析状态
    HintData aiHint_;          // 最近一次搜索的结果（只在 analyzedKey_ 与当前局面一致时有效）
    uint64_t analyzedKey_ = 0;
    bool     hasAnalysis_ = false;
    int      hintLevel_   = 3;
    bool     showHint_    = true;

    // 交互
    Coord hover_{};
    Coord selected_{};

    int    lastElapsedMs_ = 0;
    int    hoverBtn_      = -1;
    double gameOverMs_    = 0.0;
    bool   announcedOver_ = false;
};

} // namespace chess
