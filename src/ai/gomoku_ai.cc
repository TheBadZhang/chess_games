#include "ai/gomoku_ai.h"

#include "core/stopwatch.h"
#include "games/gomoku.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chess {

namespace {

constexpr int kWinScore  = 10000000;
constexpr int kInfinity  = 1 << 28;

// 搜索里用的"大分值"要留出让 alpha-beta 逐层衰减的余地：
// 直接返回 10000000 会让所有胜局看起来一样好，从而选不出"更快获胜"的路。
inline int winScoreForPly(int ply) { return kWinScore - ply * 1000; }

} // namespace

bool GomokuAi::outOfTime(Ctx& ctx) const
{
    if (ctx.aborted)
    {
        return true;
    }
    if (ctx.cancel && ctx.cancel->load(std::memory_order_relaxed))
    {
        ctx.aborted = true;
        return true;
    }
    // 时间检查的力度要和"单节点成本"匹配：
    // 每个节点动辄上百次棋型判定（约数十微秒），若每 2048 个节点才查一次，
    // 超调量会达到数秒 —— 表现是 UI 卡死几秒，而预算只有几百毫秒。
    // 这里改成每 128 个节点查一次，把超调压到可接受范围。
    if ((ctx.nodes & 127) == 0 && ctx.sw.ms() >= ctx.deadlineMs)
    {
        ctx.aborted = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  局面评估（从 side 视角）
// ---------------------------------------------------------------------------
//  做法：对每个空候选点，算"我在这里落子的价值"与"对手在这里落子的价值"，
//  取两者之和的差。这比"扫全盘数连子"更能体现"谁能先成五"。
int GomokuAi::evaluate(const GomokuGame& g, int side) const
{
    const gomoku::Board& b  = g.board();
    const int            op = (side == 1) ? 2 : 1;

    int myBest  = 0;
    int oppBest = 0;
    int mySum   = 0;
    int oppSum  = 0;

    for (int y = 0; y < b.rows; ++y)
    {
        for (int x = 0; x < b.cols; ++x)
        {
            if (!b.empty(x, y))
            {
                continue;
            }

            // 只看有棋子在附近的位置（变量名不能叫 near：windows.h 的遗留宏）
            bool hasNeighbor = false;
            for (int dy = -2; dy <= 2 && !hasNeighbor; ++dy)
            {
                for (int dx = -2; dx <= 2; ++dx)
                {
                    if (b.at(x + dx, y + dy) != 0 && b.at(x + dx, y + dy) != gomoku::Board::kWall)
                    {
                        hasNeighbor = true;
                        break;
                    }
                }
            }
            if (!hasNeighbor)
            {
                continue;
            }

            const int mine = gomoku::pointScore(b, x, y, side);
            const int his  = gomoku::pointScore(b, x, y, op);

            myBest  = std::max(myBest, mine);
            oppBest = std::max(oppBest, his);
            mySum += mine / 8;    // 多个中等威胁加起来也有价值，但别盖过单个大威胁
            oppSum += his / 8;
        }
    }

    // 进攻略优于防守（先手方尤其如此），这也更符合实战感觉
    return (mySum + myBest * 2) - (oppSum + oppBest * 2) * 1.05f;
}

int GomokuAi::moveOrderScore(const GomokuGame& g, int x, int y, int side) const
{
    const gomoku::Board& b  = g.board();
    const int            op = (side == 1) ? 2 : 1;

    const int mine    = gomoku::pointScore(b, x, y, side);
    const int his     = gomoku::pointScore(b, x, y, op);
    const int centerX = b.cols / 2;
    const int centerY = b.rows / 2;
    const int dist    = std::abs(x - centerX) + std::abs(y - centerY);

    // 自己的进攻价值 + 对手的价值（挡住对手的好点同样是"好着法"），
    // 再加一点靠近中心的偏好，让开局不散乱
    return mine + static_cast<int>(his * 0.9) - dist * 2;
}

// ---------------------------------------------------------------------------
//  负极大值搜索
// ---------------------------------------------------------------------------
int GomokuAi::negamax(const GomokuGame& g, int depth, int alpha, int beta, int side,
                      Ctx& ctx, int ply)
{
    ++ctx.nodes;

    if (outOfTime(ctx))
    {
        return 0;
    }

    if (depth <= 0)
    {
        return evaluate(g, side);
    }

    // 候选点（含合法性与禁手过滤），按启发式排序
    std::vector<Coord> cand = g.candidateMoves();
    if (cand.empty())
    {
        return 0;
    }

    std::vector<std::pair<int, Coord>> scored;
    scored.reserve(cand.size());
    for (const Coord& c : cand)
    {
        scored.emplace_back(moveOrderScore(g, c.x, c.y, side), c);
    }
    std::sort(scored.begin(), scored.end(),
              [](const std::pair<int, Coord>& a, const std::pair<int, Coord>& b) {
                  return a.first > b.first;
              });

    if (static_cast<int>(scored.size()) > ctx.candidates)
    {
        scored.resize(static_cast<size_t>(ctx.candidates));
    }

    int best = -kInfinity;

    for (const auto& [sc, c] : scored)
    {
        // 复制局面走一步。棋盘不大，拷贝成本远低于重新计算棋型。
        GomokuGame child = g;
        Move       m{};
        m.to = c;
        if (!child.apply(m))
        {
            continue;
        }

        int val;

        // 直接在子节点里判定胜负，省掉一层搜索
        if (child.status() == GameStatus::FirstWin)
        {
            val = (side == 1) ? winScoreForPly(ply) : -winScoreForPly(ply);
        }
        else if (child.status() == GameStatus::SecondWin)
        {
            val = (side == 2) ? winScoreForPly(ply) : -winScoreForPly(ply);
        }
        else
        {
            val = -negamax(child, depth - 1, -beta, -alpha, (side == 1) ? 2 : 1, ctx, ply + 1);
        }

        if (ctx.aborted)
        {
            return 0;
        }

        best = std::max(best, val);
        alpha = std::max(alpha, val);

        if (alpha >= beta)
        {
            break;   // beta 截断
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
//  VCF：连续冲四取胜搜索
// ---------------------------------------------------------------------------
//  只看"能形成冲四或成五"的着法，对手只能被动应挡。
//  这是五子棋里最有价值的专用搜索：能找到纯 alpha-beta 要很深才发现的杀棋。
bool GomokuAi::vcfSearch(const GomokuGame& g, int side, int depth, int* outX, int* outY,
                         Ctx& ctx) const
{
    if (depth <= 0)
    {
        return false;
    }

    ++ctx.nodes;
    if (outOfTime(ctx))
    {
        return false;
    }

    const gomoku::Board& b  = g.board();
    const int            op = (side == 1) ? 2 : 1;

    std::vector<Coord> cand = g.candidateMoves();

    for (const Coord& c : cand)
    {
        // 只考虑能造成"成五"或"冲四"的着法
        gomoku::Board tmp = b;
        tmp.set(c.x, c.y, static_cast<uint8_t>(side));

        if (gomoku::makesFiveOrMore(tmp, c.x, c.y, side))
        {
            *outX = c.x;
            *outY = c.y;
            return true;
        }

        const gomoku::Shape s = gomoku::bestShape(tmp, c.x, c.y, side);
        if (s != gomoku::Shape::Four)
        {
            continue;   // 活四/活三交给主搜索；VCF 只追冲四
        }

        // 走这一步，看对手有没有成五（有则这一步无效）
        GomokuGame child = g;
        Move       m{};
        m.to = c;
        if (!child.apply(m))
        {
            continue;
        }
        if (child.status() != GameStatus::Playing)
        {
            continue;
        }

        // 对手必须挡住我的成五点；枚举对手的所有着法，
        // 如果存在一个着法能让我的 VCF 断掉，则这条路不成立
        bool oppCanRefute = false;
        std::vector<Coord> oppCand = child.candidateMoves();
        for (const Coord& oc : oppCand)
        {
            GomokuGame oc2 = child;
            Move       om{};
            om.to = oc;
            if (!oc2.apply(om))
            {
                continue;
            }

            if (oc2.status() == GameStatus::SecondWin)
            {
                // 对手反过来赢了
                oppCanRefute = true;
                break;
            }

            int rx = -1;
            int ry = -1;
            if (!vcfSearch(oc2, side, depth - 1, &rx, &ry, ctx))
            {
                oppCanRefute = true;
                break;
            }
        }

        if (!oppCanRefute)
        {
            *outX = c.x;
            *outY = c.y;
            return true;
        }

        // 为了性能：对手分支通常只需试几个最像样的应对
        if (oppCand.size() > 12)
        {
            break;
        }
    }

    // 换对手视角没有意义（VCF 是单方连续进攻），这里直接返回
    (void)op;
    return false;
}

// ---------------------------------------------------------------------------
//  对外入口
// ---------------------------------------------------------------------------

AiOutcome GomokuAi::search(const IGame& position, int level, int timeBudgetMs,
                           const std::atomic<bool>& cancel, AiProgress* progress)
{
    AiOutcome out;

    const auto* game = dynamic_cast<const GomokuGame*>(&position);
    if (!game || game->isOver())
    {
        out.hint.complete = true;
        return out;
    }

    level = std::clamp(level, 1, 5);

    Stopwatch sw;
    Ctx       ctx;
    ctx.cancel     = &cancel;
    ctx.deadlineMs = timeBudgetMs > 0 ? timeBudgetMs : 400;

    switch (level)
    {
    case 1: ctx.maxDepth = 1; ctx.candidates = 6; break;
    case 2: ctx.maxDepth = 2; ctx.candidates = 8; break;
    case 3: ctx.maxDepth = 4; ctx.candidates = 10; break;
    case 4: ctx.maxDepth = 6; ctx.candidates = 12; break;
    default: ctx.maxDepth = 8; ctx.candidates = 14; break;
    }

    const int side = (game->sideToMove() == Side::First) ? 1 : 2;

    std::vector<Coord> cand = game->candidateMoves();
    if (cand.empty())
    {
        out.hint.complete = true;
        return out;
    }

    // ---- 先处理必胜 / 必挡，避免搜索在明显局面下浪费预算 ----
    //  1) 我能立刻成五 -> 直接走
    for (const Coord& c : cand)
    {
        gomoku::Board tmp = game->board();
        tmp.set(c.x, c.y, static_cast<uint8_t>(side));
        if (gomoku::makesFiveOrMore(tmp, c.x, c.y, side))
        {
            out.move    = Move{};
            out.move.to = c;
            out.hasMove = true;
            out.hint.complete   = true;
            out.hint.thinkingMs = static_cast<int>(sw.ms());
            out.hint.notes.push_back("发现直接成五，立刻取胜");
            return out;
        }
    }

    //  2) 对手能立刻成五 -> 必须挡（若多个挡不住，随便挡一个并提示）
    {
        const int op = (side == 1) ? 2 : 1;
        std::vector<Coord> mustBlock;
        for (const Coord& c : cand)
        {
            gomoku::Board tmp = game->board();
            tmp.set(c.x, c.y, static_cast<uint8_t>(op));
            if (gomoku::makesFiveOrMore(tmp, c.x, c.y, op))
            {
                mustBlock.push_back(c);
            }
        }
        if (mustBlock.size() == 1)
        {
            out.move    = Move{};
            out.move.to = mustBlock[0];
            out.hasMove = true;
            out.hint.complete   = true;
            out.hint.thinkingMs = static_cast<int>(sw.ms());
            out.hint.notes.push_back("对手下这里就成五，必须挡");
            return out;
        }
    }

    // ---- 迭代加深 ----
    //  记录上一层的最佳着法，作为本层第一个搜索的对象（PV 优先），
    //  这能让 alpha-beta 的剪枝效率显著提高。
    Coord best     = cand[0];
    Coord bestPrev = cand[0];
    int   bestScore = -kInfinity;
    int   completedDepth = 0;

    // L1：不搜索，只在"还不错的几个点"里随机
    if (level == 1)
    {
        std::vector<std::pair<int, Coord>> scored;
        for (const Coord& c : cand)
        {
            scored.emplace_back(moveOrderScore(*game, c.x, c.y, side), c);
        }
        std::sort(scored.begin(), scored.end(),
                  [](const std::pair<int, Coord>& a, const std::pair<int, Coord>& b) {
                      return a.first > b.first;
                  });
        const int topN = std::min<int>(4, static_cast<int>(scored.size()));
        best = scored[static_cast<size_t>(rng_.below(static_cast<uint32_t>(topN)))].second;
        bestScore = scored[0].first;

        out.move    = Move{};
        out.move.to = best;
        out.hasMove = true;
        out.hint.complete   = true;
        out.hint.thinkingMs = static_cast<int>(sw.ms());
        out.hint.notes.push_back("入门等级：在几个正常点里随机落子");
        if (progress)
        {
            progress->depth.store(1);
            progress->nodes.store(ctx.nodes);
        }
        return out;
    }

    for (int depth = 2; depth <= ctx.maxDepth; depth += 2)
    {
        int   alpha     = -kInfinity;
        Coord localBest = bestPrev;
        int   localScore = -kInfinity;
        bool  found      = false;

        // PV 优先：把上一层最好的点排到最前面
        std::vector<std::pair<int, Coord>> scored;
        scored.reserve(cand.size());
        for (const Coord& c : cand)
        {
            int s = moveOrderScore(*game, c.x, c.y, side);
            if (c == bestPrev)
            {
                s += kWinScore;   // 保证排在第一个
            }
            scored.emplace_back(s, c);
        }
        std::sort(scored.begin(), scored.end(),
                  [](const std::pair<int, Coord>& a, const std::pair<int, Coord>& b) {
                      return a.first > b.first;
                  });
        if (static_cast<int>(scored.size()) > ctx.candidates)
        {
            scored.resize(static_cast<size_t>(ctx.candidates));
        }

        for (const auto& [sc, c] : scored)
        {
            (void)sc;
            GomokuGame child = *game;
            Move       m{};
            m.to = c;
            if (!child.apply(m))
            {
                continue;
            }

            int val;
            if (child.status() == GameStatus::FirstWin)
            {
                val = (side == 1) ? winScoreForPly(1) : -winScoreForPly(1);
            }
            else if (child.status() == GameStatus::SecondWin)
            {
                val = (side == 2) ? winScoreForPly(1) : -winScoreForPly(1);
            }
            else
            {
                val = -negamax(child, depth - 1, -kInfinity, -alpha, (side == 1) ? 2 : 1, ctx, 1);
            }

            if (ctx.aborted)
            {
                break;
            }

            if (!found || val > localScore)
            {
                localScore = val;
                localBest  = c;
                found      = true;
            }
            alpha = std::max(alpha, val);
        }

        if (found && !ctx.aborted)
        {
            best           = localBest;
            bestPrev       = localBest;
            bestScore      = localScore;
            completedDepth = depth;

            if (progress)
            {
                progress->depth.store(depth);
                progress->nodes.store(ctx.nodes);
                progress->bestScore.store(bestScore);
                progress->hasBest.store(true);
            }

            // 已经找到必胜/必败，不必再加深
            if (bestScore >= kWinScore - 10000)
            {
                break;
            }
        }

        if (ctx.aborted)
        {
            break;
        }
    }

    // ---- L5：用 VCF 找连续冲四的杀棋 ----
    if (level >= 5 && !ctx.aborted && bestScore < kWinScore - 10000)
    {
        int vx = -1;
        int vy = -1;
        if (vcfSearch(*game, side, 8, &vx, &vy, ctx) && vx >= 0)
        {
            best      = Coord{vx, vy};
            bestScore = kWinScore - 2000;
            out.hint.notes.push_back("VCF 搜索发现连续冲四杀棋");
        }
    }

    // ---- 组织输出 ----
    out.move    = Move{};
    out.move.to = best;
    out.hasMove = true;

    out.hint.complete   = !ctx.aborted;
    out.hint.depth      = completedDepth;
    out.hint.nodes      = ctx.nodes;
    out.hint.thinkingMs = static_cast<int>(sw.ms());
    out.hint.hasEval    = true;
    out.hint.evalCp     = bestScore;

    // 胜负分换算成"胜率"与可读文本（L5 才给胜率）
    if (bestScore >= kWinScore - 10000)
    {
        out.hint.evalText = "必胜";
        if (level >= 5)
        {
            out.hint.hasWinRate = true;
            out.hint.winRate    = 1.0;
        }
    }
    else if (bestScore <= -(kWinScore - 10000))
    {
        out.hint.evalText = "必败";
        if (level >= 5)
        {
            out.hint.hasWinRate = true;
            out.hint.winRate    = 0.0;
        }
    }
    else
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%+d", bestScore);
        out.hint.evalText = buf;

        if (level >= 5)
        {
            // 用一个平滑函数把分值映射到 0..1 的"胜率"。
            // 这只是给玩家一个直观的量尺，不是严格的胜率统计。
            const double x = static_cast<double>(bestScore) / 20000.0;
            out.hint.hasWinRate = true;
            out.hint.winRate    = std::clamp(0.5 + 0.5 * std::tanh(x), 0.02, 0.98);
        }
    }

    // ---- 推荐点与主变 ----
    {
        HintCell rec;
        rec.coord  = best;
        rec.kind   = HintKind::Recommended;
        rec.weight = 1.0f;
        out.hint.cells.push_back(rec);
    }

    // ---- L3+ 的提示内容 ----
    if (level >= 3)
    {
        // 顺手把对手的成五/活四威胁也标出来，这是最需要被看到的
        const int   op = (side == 1) ? 2 : 1;
        const auto& b  = game->board();
        for (const Coord& c : cand)
        {
            if (c == best)
            {
                continue;
            }
            gomoku::Board tmp = b;
            tmp.set(c.x, c.y, static_cast<uint8_t>(op));
            const gomoku::Shape s = gomoku::bestShape(tmp, c.x, c.y, op);
            if (s == gomoku::Shape::Five || s == gomoku::Shape::Overline)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Threat;
                hc.weight = 1.0f;
                out.hint.cells.push_back(hc);
            }
            else if (s == gomoku::Shape::OpenFour)
            {
                HintCell hc;
                hc.coord  = c;
                hc.kind   = HintKind::Threat;
                hc.weight = 0.8f;
                out.hint.cells.push_back(hc);
            }
        }
    }

    // ---- L4/L5：把推演出的主变放进 lines（这里给"推荐点 + 对手最佳应手"两手的示意）----
    if (level >= 4 && out.hint.complete)
    {
        HintLine line;
        line.label = "主要变化";
        line.score = bestScore;

        Move m1{};
        m1.to = best;
        line.moves.push_back(m1);

        // 在最优着法之后，再搜一步对手的最佳应手
        GomokuGame after = *game;
        if (after.apply(m1))
        {
            const int op = (side == 1) ? 2 : 1;
            std::vector<Coord> oc = after.candidateMoves();
            int   bestVal = -kInfinity;
            Coord bestOpp{};
            for (const Coord& c : oc)
            {
                GomokuGame c2 = after;
                Move       om{};
                om.to = c;
                if (!c2.apply(om))
                {
                    continue;
                }
                const int val = -evaluate(c2, op);
                if (val > bestVal)
                {
                    bestVal = val;
                    bestOpp = c;
                }
            }
            if (bestOpp.valid())
            {
                Move m2{};
                m2.to = bestOpp;
                line.moves.push_back(m2);
            }
        }

        out.hint.lines.push_back(line);
    }

    if (level >= 5)
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "搜索深度 %d，%lld 节点，用时 %d ms", completedDepth,
                      ctx.nodes, out.hint.thinkingMs);
        out.hint.notes.push_back(buf);
    }

    if (progress)
    {
        progress->depth.store(completedDepth);
        progress->nodes.store(ctx.nodes);
    }

    nodes_ += ctx.nodes;
    return out;
}

} // namespace chess
