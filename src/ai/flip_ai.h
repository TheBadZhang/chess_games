#pragma once

#include "ai/ai.h"
#include "core/rng.h"
#include "games/flip_puzzle.h"

namespace chess {

// ---------------------------------------------------------------------------
//  翻转棋的"AI"
// ---------------------------------------------------------------------------
//  这个游戏没有对手，所以 AI 的角色是**求解器 / 代按**：
//     L1  随机按一格（能玩完，但很慢）
//     L2  贪心：选让"离全同色"最近的那一格
//     L3  精确求解器（GF(2) 线性代数）给最佳一步 + 剩余步数
//     L4  精确解 + 完整解法序列（作为主变）
//     L5  精确解 + 最少步数 + 必按格 + 解空间信息（作为"胜率"位的等价物）
//
//  提示信息的层级分工：
//     L1/L2 完全由游戏的 basicHint() 提供（不需要搜索）
//     L3+   由这里的 search() 填 cells / lines / evalText / notes
// ---------------------------------------------------------------------------
class FlipPuzzleAi : public IAI
{
public:
    const char* engineName() const override { return "GF(2) Exact"; }

    AiOutcome search(const IGame& position, int level, int timeBudgetMs,
                     const std::atomic<bool>& cancel, AiProgress* progress) override;

    std::unique_ptr<IAI> cloneForThread() const override
    {
        return std::make_unique<FlipPuzzleAi>();
    }

private:
    Rng rng_{0x1234ABCDull};
};

} // namespace chess
