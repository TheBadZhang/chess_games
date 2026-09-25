#pragma once

#include "games/game.h"

#include <atomic>

namespace chess {

// ---------------------------------------------------------------------------
//  AI 搜索结果
// ---------------------------------------------------------------------------
struct AiOutcome
{
    Move     move{};
    bool     hasMove = false;
    HintData hint;      // 只填"搜索才能得出"的部分：
                        //   recommended/good/danger 类 cells、lines
                        //   hasEval/evalCp、hasWinRate/winRate、depth/nodes
};

// ---------------------------------------------------------------------------
//  搜索进度（AI 在工作线程里更新，UI 在主线程读，全用原子量避免加锁）
// ---------------------------------------------------------------------------
struct AiProgress
{
    std::atomic<int>       depth{0};        // 已完成/正在进行的搜索深度
    std::atomic<long long> nodes{0};        // 已访问节点
    std::atomic<int>       bestScore{0};    // 当前最佳分（视游戏定义）
    std::atomic<bool>      hasBest{false};
    std::atomic<int>       playouts{0};     // MCTS 专用
};

// ---------------------------------------------------------------------------
//  AI 接口
// ---------------------------------------------------------------------------
//  实现要求：
//    * search() 可能在**工作线程**里执行，因此不得调用任何 EGE 绘图函数；
//      只能碰 position 及其 clone 的纯逻辑方法。
//    * cancel 置位或超时后必须尽快返回；允许返回部分结果
//      （此时 hint.complete 保持 false）。
//    * progress 可为空；非空时应周期性更新，供 UI 显示"思考中 L4 深度7"这类信息。
// ---------------------------------------------------------------------------
class IAI
{
public:
    virtual ~IAI() = default;

    virtual const char* engineName() const = 0;

    virtual AiOutcome search(const IGame& position, int level, int timeBudgetMs,
                             const std::atomic<bool>& cancel,
                             AiProgress* progress = nullptr) = 0;

    // 派生一个可以在工作线程里独立使用的实例。
    // 引擎可能持有置换表 / RNG 等可变状态，让工作线程用独立实例最省心，
    // 也避开了"主线程读引擎状态、工作线程写"的数据竞争。
    virtual std::unique_ptr<IAI> cloneForThread() const = 0;
};

// 各等级的默认思考时限（毫秒）。UI 允许覆盖。
int defaultTimeBudgetMs(int level);

// 各等级的简短说明（给 HUD 显示）
const char* levelShortName(int level);

} // namespace chess
