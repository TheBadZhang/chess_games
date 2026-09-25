#include "ai/xiangqi_ai.h"

#include "core/stopwatch.h"
#include "games/xiangqi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chess {

using namespace xq;

namespace {

constexpr int kInfinity = 1 << 28;
constexpr int kMateScore = 1000000;

// ---- 子力值 ----
// 象棋的量级差很大：车约为马/炮的两倍，马炮约为兵的三倍。
int baseValue(PieceType t)
{
    switch (t)
    {
    case PieceType::King:     return 60000;
    case PieceType::Chariot:  return 900;
    case PieceType::Cannon:   return 450;
    case PieceType::Horse:    return 400;
    case PieceType::Advisor:  return 200;
    case PieceType::Elephant: return 200;
    case PieceType::Pawn:     return 100;
    default:                  return 0;
    }
}

// ---- 位置微调 ----
// 只做几个"通用常识"层面的调整，不做完整的位置表：
//   * 兵过河后价值提升（越靠近对方九宫越值钱）
//   * 马炮在中心更好
//   * 车在开阔线路上更好（用是否在边线做粗略惩罚）
int positionalBonus(uint8_t piece, int x, int y)
{
    const PieceType t    = typeOf(piece);
    const uint8_t   side = sideOf(piece);
    const bool      red  = (side == kRed);
    int             b    = 0;

    // 到对方底线的距离（0 表示已到底线）
    const int advance = red ? (9 - y) : y;

    switch (t)
    {
    case PieceType::Pawn:
        if (crossedRiver(side, y))
        {
            b += 40 + advance * 8;
            // 中兵比边兵更有价值
            if (x >= 2 && x <= 6)
            {
                b += 15;
            }
        }
        break;

    case PieceType::Horse:
    case PieceType::Cannon:
        // 中心度：离第 4 路越近越好
        b += 12 - std::abs(x - 4) * 4;
        if (t == PieceType::Horse && (x == 0 || x == 8))
        {
            b -= 25;   // 马在边线活动受限
        }
        break;

    case PieceType::Chariot:
        if (x == 0 || x == 8)
        {
            b -= 10;
        }
        b += 6 - std::abs(x - 4);
        break;

    default:
        break;
    }
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
//  静态评估（红方视角为正）
// ---------------------------------------------------------------------------

int XiangqiAi::evaluateRed(const XiangqiGame& g)
{
    int score = 0;

    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kCols; ++x)
        {
            const uint8_t p = g.at(x, y);
            if (isEmpty(p))
            {
                continue;
            }
            const int v = baseValue(typeOf(p)) + positionalBonus(p, x, y);
            score += (sideOf(p) == kRed) ? v : -v;
        }
    }
    return score;
}

// ---------------------------------------------------------------------------
//  时间控制
// ---------------------------------------------------------------------------

bool XiangqiAi::outOfTime(Ctx& ctx) const
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
    // 象棋单节点成本也偏高（走法生成 + 每步合法性校验），
    // 所以同样收紧检查粒度，避免超调。
    if ((ctx.nodes & 127) == 0 && ctx.sw.ms() >= ctx.deadlineMs)
    {
        ctx.aborted = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  走法排序
// ---------------------------------------------------------------------------

int XiangqiAi::moveScore(const XiangqiGame& g, const Move& m) const
{
    const uint8_t victim   = g.at(m.to.x, m.to.y);
    const uint8_t attacker = g.at(m.from.x, m.from.y);

    if (!isEmpty(victim) && !isEmpty(attacker))
    {
        // MVV-LVA：优先"用小子吃大子"
        return 100000 + baseValue(typeOf(victim)) * 10 - baseValue(typeOf(attacker));
    }
    return 0;
}

void XiangqiAi::orderMoves(const XiangqiGame& g, std::vector<Move>& moves, const Move& ttMove,
                           int ply, const Ctx& ctx) const
{
    struct Scored
    {
        int  s;
        Move m;
    };
    std::vector<Scored> scored;
    scored.reserve(moves.size());

    for (const Move& m : moves)
    {
        int s = moveScore(g, m);
        if (ttMove.to.valid() && m == ttMove)
        {
            s += 1000000;   // 置换表给出的最佳着法优先
        }
        if (ply < 64)
        {
            if (m == ctx.killers[ply][0])
            {
                s += 500000;
            }
            else if (m == ctx.killers[ply][1])
            {
                s += 400000;
            }
        }
        scored.push_back({s, m});
    }

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.s > b.s; });
    for (size_t i = 0; i < scored.size(); ++i)
    {
        moves[i] = scored[i].m;
    }
}

// ---------------------------------------------------------------------------
//  静态搜索（只搜吃子）
// ---------------------------------------------------------------------------
int XiangqiAi::quiescence(const XiangqiGame& g, int alpha, int beta, uint8_t side, int ply,
                          Ctx& ctx)
{
    ++ctx.nodes;
    if (outOfTime(ctx))
    {
        return 0;
    }

    const int standPat = (side == kRed) ? evaluateRed(g) : -evaluateRed(g);
    if (standPat >= beta)
    {
        return beta;
    }
    if (standPat > alpha)
    {
        alpha = standPat;
    }
    if (ply >= 32)
    {
        return alpha;
    }

    std::vector<Move> all;
    g.genPseudoMoves(side, all);

    // 只保留吃子
    std::vector<Move> caps;
    for (const Move& m : all)
    {
        if (!isEmpty(g.at(m.to.x, m.to.y)) && g.isLegal(m))
        {
            caps.push_back(m);
        }
    }
    orderMoves(g, caps, Move{}, ply, ctx);

    for (const Move& m : caps)
    {
        XiangqiGame child = g;
        if (!child.apply(m))
        {
            continue;
        }

        const int val = -quiescence(child, -beta, -alpha, (side == kRed) ? kBlack : kRed,
                                    ply + 1, ctx);
        if (ctx.aborted)
        {
            return 0;
        }
        if (val >= beta)
        {
            return beta;
        }
        if (val > alpha)
        {
            alpha = val;
        }
    }
    return alpha;
}

// ---------------------------------------------------------------------------
//  主搜索
// ---------------------------------------------------------------------------

int XiangqiAi::negamax(const XiangqiGame& g, int depth, int alpha, int beta, uint8_t side,
                       int ply, Ctx& ctx)
{
    ++ctx.nodes;
    if (outOfTime(ctx))
    {
        return 0;
    }

    // 将死 / 困毙：无着可走
    const auto moves = g.legalMoves();
    if (moves.empty())
    {
        // 越早被将死越糟，所以用 ply 调整分值，让引擎偏好"晚输/早赢"
        return -kMateScore + ply;
    }
    if (depth <= 0)
    {
        return quiescence(g, alpha, beta, side, ply, ctx);
    }

    // 置换表探查
    const uint64_t key    = g.stateKey();
    TTEntry&       entry  = tt_[key & (tt_.size() - 1)];
    Move           ttMove{};
    if (entry.key == key)
    {
        ttMove = entry.best;
        if (entry.depth >= depth)
        {
            if (entry.flag == 0)
            {
                return entry.score;
            }
            if (entry.flag == 1 && entry.score >= beta)
            {
                return entry.score;
            }
            if (entry.flag == 2 && entry.score <= alpha)
            {
                return entry.score;
            }
        }
    }

    std::vector<Move> ordered = moves;
    orderMoves(g, ordered, ttMove, ply, ctx);

    const int alphaOrig = alpha;
    int       best      = -kInfinity;
    Move      bestMove{};

    int moveIndex = 0;
    for (const Move& m : ordered)
    {
        XiangqiGame child = g;
        if (!child.apply(m))
        {
            continue;
        }

        // 将军延伸：如果这步棋给对手造成将军，就多搜一层
        int extend = 0;
        const uint8_t them = (side == kRed) ? kBlack : kRed;
        if (child.inCheck(them))
        {
            extend = 1;
        }

        const int val = -negamax(child, depth - 1 + extend, -beta, -alpha,
                                 (side == kRed) ? kBlack : kRed, ply + 1, ctx);
        if (ctx.aborted)
        {
            return 0;
        }

        if (val > best)
        {
            best     = val;
            bestMove = m;
        }
        if (val > alpha)
        {
            alpha = val;
        }
        if (alpha >= beta)
        {
            // 记录 killer（安静着法才有价值作为 killer）
            if (isEmpty(g.at(m.to.x, m.to.y)) && ply < 64)
            {
                if (ctx.killers[ply][0] != m)
                {
                    ctx.killers[ply][1] = ctx.killers[ply][0];
                    ctx.killers[ply][0] = m;
                }
            }
            break;
        }
        ++moveIndex;
    }

    // 写回置换表
    if (entry.key != key || entry.depth <= depth)
    {
        entry.key   = key;
        entry.depth = depth;
        entry.score = best;
        entry.best  = bestMove;
        entry.flag  = (best <= alphaOrig) ? 2 : ((best >= beta) ? 1 : 0);
    }

    return best;
}

// ---------------------------------------------------------------------------
//  对外入口
// ---------------------------------------------------------------------------

AiOutcome XiangqiAi::search(const IGame& position, int level, int timeBudgetMs,
                            const std::atomic<bool>& cancel, AiProgress* progress)
{
    AiOutcome out;

    const auto* game = dynamic_cast<const XiangqiGame*>(&position);
    if (!game || game->isOver())
    {
        out.hint.complete = true;
        return out;
    }

    level = std::clamp(level, 1, 5);

    Stopwatch sw;
    Ctx       ctx;
    ctx.cancel     = &cancel;
    ctx.deadlineMs = timeBudgetMs > 0 ? timeBudgetMs : 1000;

    int maxDepth = 4;
    switch (level)
    {
    case 1: maxDepth = 1; break;
    case 2: maxDepth = 2; break;
    case 3: maxDepth = 4; break;
    case 4: maxDepth = 6; break;
    default: maxDepth = 8; break;
    }

    const uint8_t side = (game->sideToMove() == Side::First) ? kRed : kBlack;

    const auto rootMoves = game->legalMoves();
    if (rootMoves.empty())
    {
        out.hint.complete = true;
        return out;
    }

    Move best        = rootMoves[0];
    int  bestScore   = -kInfinity;
    int  completed   = 0;
    bool foundMate   = false;

    // ---- 迭代加深 ----
    for (int depth = 1; depth <= maxDepth; ++depth)
    {
        std::vector<Move> ordered = rootMoves;
        // 上一层的最佳着法排到最前
        std::sort(ordered.begin(), ordered.end(), [&](const Move& a, const Move& b) {
            if (a == best) { return true; }
            if (b == best) { return false; }
            return moveScore(*game, a) > moveScore(*game, b);
        });

        int   alpha     = -kInfinity;
        int   localBest = -kInfinity;
        Move  localMove = ordered[0];
        bool  any       = false;

        for (const Move& m : ordered)
        {
            XiangqiGame child = *game;
            if (!child.apply(m))
            {
                continue;
            }

            const uint8_t them   = (side == kRed) ? kBlack : kRed;
            const int     extend = child.inCheck(them) ? 1 : 0;

            const int val = -negamax(child, depth - 1 + extend, -kInfinity, -alpha, them, 1, ctx);
            if (ctx.aborted)
            {
                break;
            }

            if (!any || val > localBest)
            {
                localBest = val;
                localMove = m;
                any       = true;
            }
            if (val > alpha)
            {
                alpha = val;
            }
        }

        if (any && !ctx.aborted)
        {
            best      = localMove;
            bestScore = localBest;
            completed = depth;

            if (progress)
            {
                progress->depth.store(depth);
                progress->nodes.store(ctx.nodes);
                progress->bestScore.store(bestScore);
                progress->hasBest.store(true);
            }

            // 已经看到杀棋，不必再加深
            if (std::abs(bestScore) > kMateScore - 1000)
            {
                foundMate = true;
                break;
            }
        }

        if (ctx.aborted)
        {
            break;
        }
    }

    // ---- 组织输出 ----
    out.move    = best;
    out.hasMove = true;

    out.hint.complete   = !ctx.aborted;
    out.hint.depth      = completed;
    out.hint.nodes      = ctx.nodes;
    out.hint.thinkingMs = static_cast<int>(sw.ms());
    out.hint.hasEval    = true;

    // 把"红方为正"的分数换算成"当前行棋方为正"
    const int sideScore = (side == kRed) ? bestScore : -bestScore;
    out.hint.evalCp     = sideScore;

    // 换算成"兵"为单位更符合象棋习惯（1 个兵 ≈ 100）
    char buf[96];
    if (std::abs(bestScore) > kMateScore - 1000)
    {
        const bool winning = (sideScore > 0);
        std::snprintf(buf, sizeof(buf), "%s", winning ? "有杀棋" : "被将死");
        out.hint.evalText = buf;
        if (level >= 5)
        {
            out.hint.hasWinRate = true;
            out.hint.winRate    = winning ? 1.0 : 0.0;
        }
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%+.2f 子", sideScore / 100.0);
        out.hint.evalText = buf;
        if (level >= 5)
        {
            // 用平滑函数把子力差映射到 0..1 的"胜率"。这是量尺，不是统计值。
            const double x = static_cast<double>(sideScore) / 800.0;
            out.hint.hasWinRate = true;
            out.hint.winRate    = std::clamp(0.5 + 0.5 * std::tanh(x), 0.02, 0.98);
        }
    }

    // 推荐着法
    {
        HintCell rec;
        rec.coord  = best.to;
        rec.kind   = HintKind::Recommended;
        rec.weight = 1.0f;
        out.hint.cells.push_back(rec);

        // 起点也标一下，让箭头有来源
        HintCell from;
        from.coord  = best.from;
        from.kind   = HintKind::Good;
        from.weight = 0.8f;
        out.hint.cells.push_back(from);
    }

    // L4+：主变（推荐着法 + 对手最佳应手）
    if (level >= 4 && out.hint.complete)
    {
        HintLine line;
        line.label = "主要变化";
        line.score = sideScore;
        line.moves.push_back(best);

        XiangqiGame after = *game;
        if (after.apply(best))
        {
            const uint8_t them = (side == kRed) ? kBlack : kRed;
            const auto    rep  = after.legalMoves();

            int   bestVal = -kInfinity;
            Move  bestRep{};
            for (const Move& r : rep)
            {
                XiangqiGame c2 = after;
                if (!c2.apply(r))
                {
                    continue;
                }
                const int v = -evaluateRed(c2);
                const int val = (them == kRed) ? v : -v;
                if (val > bestVal)
                {
                    bestVal = val;
                    bestRep = r;
                }
            }
            if (bestRep.to.valid())
            {
                line.moves.push_back(bestRep);
            }
        }
        out.hint.lines.push_back(line);
    }

    // 提示：把对手能将军的点标出来（最重要的一条预警）
    if (level >= 3)
    {
        for (const Coord& c : game->opponentCheckTargets())
        {
            HintCell hc;
            hc.coord  = c;
            hc.kind   = HintKind::Threat;
            hc.weight = 0.75f;
            out.hint.cells.push_back(hc);
        }
    }

    if (level >= 5)
    {
        std::snprintf(buf, sizeof(buf), "搜索深度 %d，%lld 节点，用时 %d ms", completed,
                      ctx.nodes, out.hint.thinkingMs);
        out.hint.notes.push_back(buf);
        if (foundMate)
        {
            out.hint.notes.push_back("已找到强制杀棋路线");
        }
    }

    if (progress)
    {
        progress->depth.store(completed);
        progress->nodes.store(ctx.nodes);
    }
    return out;
}

} // namespace chess
