#pragma once

#include <chrono>

namespace chess {

// ---------------------------------------------------------------------------
//  单调计时器
// ---------------------------------------------------------------------------
//  AI 思考时限、帧间隔、TPS 都用它。刻意用 steady_clock：
//  system_clock 会被系统时间调整影响，用它做时限是不可靠的。
// ---------------------------------------------------------------------------
class Stopwatch
{
public:
    Stopwatch() { reset(); }

    void reset() { t0_ = Clock::now(); }

    double ms() const
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0_).count();
    }

    bool expired(double limitMs) const { return ms() >= limitMs; }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point t0_;
};

// ---------------------------------------------------------------------------
//  帧计时：记录上一帧与最近若干帧的平均间隔
// ---------------------------------------------------------------------------
class FrameClock
{
public:
    double tick()
    {
        const double now = sw_.ms();
        const double dt  = now - last_;
        last_ = now;

        // 首帧与异常长帧（例如窗口被拖动后）不参与平滑，避免动画突跳
        if (dt > 0.0 && dt < 500.0)
        {
            accum_ += dt;
            ++count_;
        }
        return dt;
    }

    // 平均帧间隔（毫秒）；样本不足时返回 0
    double avgMs() const { return count_ > 0 ? accum_ / count_ : 0.0; }

    // 平均帧率
    double fps() const
    {
        const double a = avgMs();
        return a > 0.0 ? 1000.0 / a : 0.0;
    }

private:
    Stopwatch sw_{};
    double    last_  = 0.0;
    double    accum_ = 0.0;
    long long count_ = 0;
};

} // namespace chess
