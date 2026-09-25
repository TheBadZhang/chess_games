#pragma once

#include "ai/ai.h"
#include "core/rng.h"
#include "core/stopwatch.h"

#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  五子棋 AI
// ---------------------------------------------------------------------------
//  引擎：负极大值 + alpha-beta + 迭代加深 + 候选点启发式排序
//
//  等级划分：
//     L1  在较好的几个点里随机（会下，但不认真）
//     L2  深度 2，候选 8      —— 只看一步交换，能挡成五
//     L3  深度 4，候选 10     —— 有活三/冲四意识
//     L4  深度 6，候选 12     —— 会做双威胁
//     L5  深度 8，候选 14 + VCF（连续冲四胜）搜索
//
//  为什么必须剪候选：15x15 全盘 225 个点、深度 6 的分支是天文数字。
//  "只看已有棋子 2 格邻域"是五子棋的通用剪枝，实战几乎不丢好点。
// ---------------------------------------------------------------------------
class GomokuAi : public IAI
{
public:
    GomokuAi() = default;
    explicit GomokuAi(uint64_t seed) : rng_(seed) {}

    const char* engineName() const override { return "Alpha-Beta + VCF"; }

    AiOutcome search(const IGame& position, int level, int timeBudgetMs,
                     const std::atomic<bool>& cancel, AiProgress* progress) override;

    std::unique_ptr<IAI> cloneForThread() const override
    {
        auto p = std::make_unique<GomokuAi>();
        p->nodes_ = 0;
        return p;
    }

private:
    // 搜索上下文（在 search 内部初始化）
    struct Ctx
    {
        const std::atomic<bool>* cancel     = nullptr;
        double                   deadlineMs = 0;
        long long                nodes      = 0;
        int                      maxDepth   = 0;
        int                      candidates = 10;
        bool                     aborted    = false;

        // 计时器必须是**每次搜索都重置**的实例成员。
        // 曾经把它写成 outOfTime() 里的 `static Stopwatch sw;` —— 函数局部静态只
        // 初始化一次，而 deadlineMs 是相对预算，于是第一次搜索之后的所有搜索
        // 都会立刻判定超时，AI 表面在下棋、实际完全没用时间预算。
        class Stopwatch sw{};
    };

    int  negamax(const class GomokuGame& g, int depth, int alpha, int beta, int side,
                 Ctx& ctx, int ply);
    int  evaluate(const class GomokuGame& g, int side) const;
    int  moveOrderScore(const class GomokuGame& g, int x, int y, int side) const;
    bool outOfTime(Ctx& ctx) const;

    // VCF：只考虑"冲四"和"成五"的连续进攻搜索，用于 L5
    bool vcfSearch(const class GomokuGame& g, int side, int depth, int* outX, int* outY,
                   Ctx& ctx) const;

    mutable Rng rng_{0x5EED1234ull};
    mutable long long nodes_ = 0;
};

} // namespace chess
