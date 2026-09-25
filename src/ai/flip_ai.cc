#include "ai/flip_ai.h"

#include "core/stopwatch.h"
#include "games/registry.h"

#include <algorithm>
#include <cstdio>

namespace chess {

AiOutcome FlipPuzzleAi::search(const IGame& position, int level, int timeBudgetMs,
                               const std::atomic<bool>& cancel, AiProgress* progress)
{
    (void)timeBudgetMs;

    AiOutcome out;

    const auto* game = dynamic_cast<const FlipPuzzleGame*>(&position);
    if (!game)
    {
        return out;
    }

    const int cols = game->cols();
    const int rows = game->rows();
    const int n    = cols * rows;
    if (n <= 0)
    {
        return out;
    }

    Stopwatch sw;

    progress->depth.store(1);
    progress->nodes.store(n);

    const auto& cells = game->cells();

    // ---- L1：随机 ----
    if (level <= 1)
    {
        const int idx = rng_.nextInt(0, n - 1);
        out.move.to = Coord{idx % cols, idx / cols};
        out.hasMove = true;
        out.hint.complete = true;
        out.hint.thinkingMs = static_cast<int>(sw.ms());
        return out;
    }

    // ---- L2：贪心（选让势下降最多的一格）----
    if (level == 2)
    {
        int     bestDelta = 0;
        int     bestIdx   = -1;
        const int cur     = flip::potential(cells);

        for (int i = 0; i < n; ++i)
        {
            if (cancel.load(std::memory_order_relaxed))
            {
                break;
            }

            std::vector<flip::Cell> tmp = cells;
            flip::applyPress(tmp, cols, rows, i % cols, i / cols);
            const int delta = flip::potential(tmp) - cur;
            if (bestIdx < 0 || delta < bestDelta)
            {
                bestDelta = delta;
                bestIdx   = i;
            }
        }

        if (bestIdx < 0)
        {
            bestIdx = rng_.nextInt(0, n - 1);
        }

        out.move.to = Coord{bestIdx % cols, bestIdx / cols};
        out.hasMove = true;
        out.hint.complete = true;
        out.hint.thinkingMs = static_cast<int>(sw.ms());

        char buf[96];
        std::snprintf(buf, sizeof(buf), "贪心：这一步可让落后数变化 %+d", bestDelta);
        out.hint.notes.push_back(buf);
        return out;
    }

    // ---- L3+：精确求解 ----
    const flip::Solution& sol = game->solution();
    if (!sol.found || sol.pressCount == 0)
    {
        out.hint.complete = true;
        out.hint.thinkingMs = static_cast<int>(sw.ms());
        return out;
    }

    // 选一步：优先"必按格"（在所有解里都要按），否则取权重最高的
    int pick = -1;
    for (int i = 0; i < n; ++i)
    {
        if (static_cast<int>(sol.required.size()) > i && sol.required[i])
        {
            pick = i;
            break;
        }
    }
    if (pick < 0)
    {
        for (int i = 0; i < n; ++i)
        {
            if (sol.press[i])
            {
                pick = i;
                break;
            }
        }
    }
    if (pick < 0)
    {
        out.hint.complete = true;
        out.hint.thinkingMs = static_cast<int>(sw.ms());
        return out;
    }

    out.move.to = Coord{pick % cols, pick / cols};
    out.hasMove = true;

    // ---- 提示内容 ----
    HintData& h = out.hint;

    HintCell rec;
    rec.coord  = out.move.to;
    rec.kind   = HintKind::Recommended;
    rec.weight = 1.0f;
    h.cells.push_back(rec);

    // L4+：完整解法序列作为主变
    if (level >= 4)
    {
        HintLine line;
        line.label = "解法序列（按顺序点）";
        for (int i = 0; i < n; ++i)
        {
            if (!sol.press[i])
            {
                continue;
            }
            Move mv{};
            mv.to = Coord{i % cols, i / cols};
            line.moves.push_back(mv);
        }
        line.score = -sol.pressCount;
        h.lines.push_back(line);
    }

    h.hasEval  = true;
    h.evalCp   = -sol.pressCount;
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "还需 %d 步", sol.pressCount);
        h.evalText = buf;
    }

    h.depth = 1;
    h.nodes = n;

    // L5：把一个"完成度"映射到 0..1 当作胜率位的等价物：
    // 已完成的接近程度（落后数越小越接近完成）
    if (level >= 5)
    {
        const int pot  = flip::potential(cells);
        const int maxP = std::max(1, n / 2);
        h.hasWinRate = true;
        h.winRate    = std::clamp(1.0 - static_cast<double>(pot) / maxP, 0.0, 1.0);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "解空间维数 %d（共 %d 组解）", sol.nullity,
                      sol.nullity < 20 ? (1 << sol.nullity) : -1);
        h.notes.push_back(buf);

        int requiredCount = 0;
        for (uint8_t v : sol.required)
        {
            requiredCount += v;
        }
        std::snprintf(buf, sizeof(buf), "其中 %d 格为所有解必按（已用蓝圈标出）", requiredCount);
        h.notes.push_back(buf);
    }

    h.complete   = true;
    h.thinkingMs = static_cast<int>(sw.ms());

    // 精确求解器瞬间出结果，进度条不必假装在思考
    progress->depth.store(level >= 4 ? 2 : 1);

    return out;
}

} // namespace chess
