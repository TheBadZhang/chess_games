#include "app/ai_worker.h"

#include "core/stopwatch.h"

namespace chess {

namespace {

// 进程启动以来的单调毫秒数，用于统计耗时
long long nowMs()
{
    static const Stopwatch sw;
    return static_cast<long long>(sw.ms());
}

} // namespace

AiWorker::~AiWorker()
{
    cancelAndJoin();
}

void AiWorker::start(std::unique_ptr<IGame> position,
                     std::unique_ptr<IAI>   ai,
                     int                    level,
                     int                    timeBudgetMs)
{
    // 先把上一次收干净，避免两次线程同时存在
    cancelAndJoin();

    if (!position || !ai)
    {
        return;
    }

    cancel_.store(false, std::memory_order_release);
    ready_.store(false, std::memory_order_release);

    // AiProgress 全是原子量，没有拷贝/移动赋值，只能逐个复位
    progress_.depth.store(0, std::memory_order_relaxed);
    progress_.nodes.store(0, std::memory_order_relaxed);
    progress_.bestScore.store(0, std::memory_order_relaxed);
    progress_.hasBest.store(false, std::memory_order_relaxed);
    progress_.playouts.store(0, std::memory_order_relaxed);

    startedAtMs_.store(nowMs(), std::memory_order_release);

    running_.store(true, std::memory_order_release);

    thread_ = std::thread(
        [this, pos = std::shared_ptr<IGame>(std::move(position)),
               engine = std::shared_ptr<IAI>(std::move(ai)), level, timeBudgetMs]() mutable
        {
            AiOutcome result;
            try
            {
                result = engine->search(*pos, level, timeBudgetMs, cancel_, &progress_);
            }
            catch (...)
            {
                // 工作线程里抛异常会直接 terminate，这里兜住：
                // 搜索失败时保持 hasMove = false，UI 会走"AI 无法行动"的分支
                result = AiOutcome{};
            }

            if (cancel_.load(std::memory_order_acquire))
            {
                result.hint.complete = false;
            }

            {
                std::lock_guard<std::mutex> lock(mtx_);
                outcome_ = std::move(result);
            }

            lastElapsedMs_.store(
                static_cast<int>(nowMs() - startedAtMs_.load(std::memory_order_acquire)),
                std::memory_order_release);

            // 先置 ready 再清 running：poll() 只在见到 ready 时取结果
            ready_.store(true, std::memory_order_release);
            running_.store(false, std::memory_order_release);
        });
}

void AiWorker::cancel()
{
    cancel_.store(true, std::memory_order_release);
}

bool AiWorker::poll(AiOutcome& out, int* elapsedMs)
{
    joinIfFinished();

    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        out = std::move(outcome_);
        outcome_ = AiOutcome{};
    }

    ready_.store(false, std::memory_order_release);

    if (elapsedMs)
    {
        *elapsedMs = lastElapsedMs_.load(std::memory_order_acquire);
    }
    return true;
}

int AiWorker::elapsedMs() const
{
    if (running_.load(std::memory_order_acquire))
    {
        return static_cast<int>(nowMs() - startedAtMs_.load(std::memory_order_acquire));
    }
    return lastElapsedMs_.load(std::memory_order_acquire);
}

void AiWorker::cancelAndJoin()
{
    cancel_.store(true, std::memory_order_release);

    if (thread_.joinable())
    {
        thread_.join();
    }

    // 线程已结束，把可能残留的 ready 清掉，避免下次 poll 取到旧结果
    ready_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

void AiWorker::joinIfFinished()
{
    // 只有在线程已经结束（running == false）时才 join，保证非阻塞
    if (thread_.joinable() && !running_.load(std::memory_order_acquire))
    {
        thread_.join();
    }
}

} // namespace chess
