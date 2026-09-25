#pragma once

#include "ai/ai.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace chess {

// ---------------------------------------------------------------------------
//  AI 工作线程
// ---------------------------------------------------------------------------
//  设计要点（对应"考量 3 选 A：单工作线程 + 取消位"）：
//
//    * 只有一个工作线程，同一时刻只跑一次搜索。EGE 的消息循环只在主线程，
//      工作线程只做纯计算（不碰任何 EGE 函数）。
//    * 搜索对象是**局面的深拷贝**。因此主线程在 AI 思考期间仍可以悔棋/重开，
//      不会与 AI 读写同一份状态。
//    * 取消是协作式的：置 cancel_ 后由 AI 实现自己检查并尽快返回。
//      因此 cancelAndJoin() 一般很快就返回；围棋 MCTS 的实现必须频繁检查。
//    * 结果通过 poll() 交回主线程，UI 线程不做加锁等待。
// ---------------------------------------------------------------------------
class AiWorker
{
public:
    AiWorker() = default;
    ~AiWorker();

    AiWorker(const AiWorker&)            = delete;
    AiWorker& operator=(const AiWorker&) = delete;

    // 启动一次搜索。会先取消并回收上一次（若有）。
    // position 是当前局面的深拷贝；ai 是引擎实例（可为空 → 不启动）。
    void start(std::unique_ptr<IGame> position,
               std::unique_ptr<IAI>   ai,
               int                    level,
               int                    timeBudgetMs);

    // 请求取消（非阻塞）
    void cancel();

    // 是否仍在搜索
    bool running() const { return running_.load(std::memory_order_acquire); }

    // 结果是否就绪。就绪时取走结果并复位（之后 running() 变 false）。
    bool poll(AiOutcome& out, int* elapsedMs = nullptr);

    // 取消并等待线程结束。离开对局场景 / 退出程序时必须调用。
    void cancelAndJoin();

    // 供 UI 读的实时进度
    const AiProgress& progress() const { return progress_; }

    // 本次搜索已经跑了多久（毫秒）；未在跑时返回上次耗时
    int elapsedMs() const;

private:
    void joinIfFinished();

    std::thread             thread_;
    std::atomic<bool>       cancel_{false};
    std::atomic<bool>       running_{false};
    std::atomic<bool>       ready_{false};
    std::atomic<long long>  startedAtMs_{0};
    std::atomic<int>        lastElapsedMs_{0};

    std::mutex  mtx_;
    AiOutcome   outcome_{};

    AiProgress  progress_{};
};

} // namespace chess
