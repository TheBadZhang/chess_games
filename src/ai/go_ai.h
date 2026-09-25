#pragma once

#include "ai/ai.h"
#include "core/rng.h"
#include "core/stopwatch.h"

#include <cstdint>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  围棋 AI
// ---------------------------------------------------------------------------
//  引擎：启发式（L1/L2） + 蒙特卡洛树搜索 MCTS（L3/L4/L5）
//
//  为什么分级要跨两种方法：
//    9 路以下用随机模拟就能走出"看得过去"的棋；但 19 路随机模拟质量很差，
//    所以高阶只是加大预算（更多 playout），并不是"一定强"。
//    这一点在 UI 的等级说明里已经写明（"19 路能下但弱"）。
//
//  等级：
//     L1  纯随机合法着手（避开自填眼），不做任何评价
//     L2  一步启发式：能提子就提、被打吃就跑、避免自陷打吃、占空点
//     L3  MCTS，约 800 playout 或时限
//     L4  MCTS，约 3000 playout
//     L5  MCTS，约 8000 playout
//
//  内部使用一份**精简局面**（FastPos）而不是 GoGame：
//    MCTS 每次模拟都要落子/回滚数百次，用 IGame 的深拷贝（含 vector 历史）
//    会慢一个数量级以上。FastPos 用固定数组 + 落子记录做 O(1) 回滚。
// ---------------------------------------------------------------------------
class GoAi : public IAI
{
public:
    GoAi() = default;
    explicit GoAi(uint64_t seed) : rng_(seed) {}

    const char* engineName() const override { return "MCTS + Heuristic"; }

    AiOutcome search(const IGame& position, int level, int timeBudgetMs,
                     const std::atomic<bool>& cancel, AiProgress* progress) override;

    std::unique_ptr<IAI> cloneForThread() const override
    {
        return std::make_unique<GoAi>();
    }

private:
    // 精简棋盘：0 空 / 1 黑 / 2 白 / 3 界外
    struct FastPos
    {
        int                  n = 9;          // 边长（正方形）
        std::vector<uint8_t> cells;
        uint8_t              toMove  = 1;
        int                  koPoint = -1;   // 简单劫点（-1 = 无）
        int                  passes  = 0;

        void reset(int size);
        uint8_t at(int i) const { return cells[static_cast<size_t>(i)]; }
        bool    inBoard(int x, int y) const { return x >= 0 && y >= 0 && x < n && y < n; }
        int     idx(int x, int y) const { return y * n + x; }
    };

    // 一次落子的回滚记录
    struct PlayRec
    {
        int                  point    = -1;   // -1 表示 pass
        uint8_t              player   = 0;
        int                  koBefore = -1;
        int                  passesBefore = 0;
        std::vector<int>     removed;         // 被提掉的位置
        std::vector<uint8_t> removedColor;
    };

    // ---- 基础操作 ----
    static int  countLibs(const FastPos& p, int start, uint8_t stone);
    static int  removeGroup(FastPos& p, int start, uint8_t stone, std::vector<int>* out);
    static bool isEye(const FastPos& p, int point, uint8_t stone);
    static bool tryPlay(FastPos& p, int point, PlayRec* rec);
    static void undoPlay(FastPos& p, const PlayRec& rec);
    static bool isLegalMove(const FastPos& p, int point);

    // ---- 模拟 ----
    // cancel/playoutDeadline 让单次模拟也能及时中断：
    // 19 路的一次 playout 可能走 700+ 手，没有内部检查的话
    // 取消请求要等这一局模拟完才生效，UI 会明显卡顿。
    // 返回本次模拟实际走了多少手（用于诊断 playout 是否被截断）。
    int playout(FastPos& p, int maxMoves, std::vector<PlayRec>& hist,
                const std::atomic<bool>* cancel = nullptr,
                double playoutDeadlineMs = 0.0);
    // ---- 评价 ----
    // 中国规则数子（不含贴目），返回黑 - 白
    static int areaScoreBlackMinusWhite(const FastPos& p);
    // 从 toMove 视角把盘面差换成"胜"的判定
    static bool blackWinsByArea(const FastPos& p, double komi);

    int heuristicPick(const FastPos& p, int level, std::vector<int>& outOrder);

    mutable Rng rng_{0xA5A5C0DEull};

    // 计时器：**每次 search() 开始时重置**，search 内部所有"与时限比较"
    // 的地方（包括 playout 内部的超时检查）都必须用它。
    //
    // 两种写法都是错的，且症状同为"AI 看起来在下棋但完全没用时间预算"：
    //   * 函数局部 static Stopwatch（只初始化一次，永不重置）
    //   * 用进程级累计时间去比**相对**预算（进程跑过一会后恒为超时）
    Stopwatch clock_;
    double    deadlineMs_ = 0.0;
};

} // namespace chess
