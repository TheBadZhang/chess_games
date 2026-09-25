#pragma once

#include "ai/ai.h"
#include "core/stopwatch.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace chess {

class XiangqiGame;

// ---------------------------------------------------------------------------
//  象棋 AI
// ---------------------------------------------------------------------------
//  引擎：alpha-beta + 迭代加深 + 置换表 + 吃子静态搜索 + killer 走法排序
//
//  等级：
//     L1  深度 1（看得见吃子，不做交换判断）
//     L2  深度 2
//     L3  深度 4
//     L4  深度 6
//     L5  深度 8 + 静态搜索更深
//
//  评价：子力值 + 位置表 + 机动性。象棋的子力值有明显的量级差
//  （车 ≫ 马炮 ≫ 兵卒），所以位置表只做微调。
// ---------------------------------------------------------------------------
class XiangqiAi : public IAI
{
public:
    const char* engineName() const override { return "Alpha-Beta + TT"; }

    AiOutcome search(const IGame& position, int level, int timeBudgetMs,
                     const std::atomic<bool>& cancel, AiProgress* progress) override;

    std::unique_ptr<IAI> cloneForThread() const override
    {
        auto p = std::make_unique<XiangqiAi>();
        // 注意：这里必须"重置内容"而不是 clear()。
        // 索引用的是 key & (tt_.size() - 1)；一旦 size() == 0，
        // size() - 1 会下溢成极大值，第一次探查就数组越界。
        std::fill(p->tt_.begin(), p->tt_.end(), TTEntry{});
        return p;
    }

    // 供自检使用：某个局面的静态评估（红方视角为正）
    static int evaluateRed(const XiangqiGame& g);

private:
    struct Ctx
    {
        const std::atomic<bool>* cancel     = nullptr;
        double                   deadlineMs = 0;
        long long                nodes      = 0;
        bool                     aborted    = false;

        // 计时器必须每次搜索都重置（不能写成函数局部 static，
        // 那种写法只初始化一次，会让后续搜索立刻超时）。
        class Stopwatch sw{};

        // killer：每一层记录两个引起截断的"安静着法"，用于排序
        Move killers[64][2]{};
    };

    struct TTEntry
    {
        uint64_t key    = 0;
        int      depth  = 0;
        int      score  = 0;
        int      flag   = 0;   // 0 = exact, 1 = lower, 2 = upper
        Move     best{};
    };

    int  negamax(const XiangqiGame& g, int depth, int alpha, int beta, uint8_t side, int ply,
                 Ctx& ctx);
    int  quiescence(const XiangqiGame& g, int alpha, int beta, uint8_t side, int ply, Ctx& ctx);
    bool outOfTime(Ctx& ctx) const;
    void orderMoves(const XiangqiGame& g, std::vector<Move>& moves, const Move& ttMove,
                    int ply, const Ctx& ctx) const;
    int  moveScore(const XiangqiGame& g, const Move& m) const;

    std::vector<TTEntry> tt_ = std::vector<TTEntry>(1 << 16);
};

} // namespace chess
