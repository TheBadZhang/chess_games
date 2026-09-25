#include "ai/go_ai.h"

#include "core/stopwatch.h"
#include "games/go.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chess {

namespace {

constexpr int kOrtho[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int kDiag[4][2]  = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// MCTS 节点：放在一个扁平数组里，避免每个节点一次堆分配
struct Node
{
    int    move        = -1;   // 走到本节点的那一手（-1 = pass）
    int    parent      = -1;
    int    firstChild  = -1;   // 子节点链表的头
    int    nextSibling = -1;   // 兄弟链表
    int    childCount  = 0;
    double wins        = 0.0;  // "走到本节点的那一手"的下方赢了几次
    int    visits      = 0;

    // 尚未展开的候选着法（生成时已打乱）。
    //
    // 这里必须逐次"取出一个"来扩展。早期版本是每次随机挑一个着法新建子节点，
    // 但不记录哪些还没试过 —— 结果是同一个节点反复添加（或漏掉）着法，
    // 树实际上退化成一条链，MCTS 名存实亡（等同于随机游走）。
    std::vector<int> untried;
};

} // namespace

// ---------------------------------------------------------------------------
//  FastPos 基础操作
// ---------------------------------------------------------------------------

void GoAi::FastPos::reset(int size)
{
    n = size;
    cells.assign(static_cast<size_t>(n) * n, 0);
    toMove  = 1;
    koPoint = -1;
    passes  = 0;
}

int GoAi::countLibs(const FastPos& p, int start, uint8_t stone)
{
    if (p.at(start) != stone)
    {
        return -1;
    }

    const int total = p.n * p.n;
    std::vector<uint8_t> seen(total, 0);
    std::vector<uint8_t> libSeen(total, 0);
    std::vector<int>     stack;

    stack.push_back(start);
    seen[start] = 1;

    int libs = 0;
    while (!stack.empty())
    {
        const int pt = stack.back();
        stack.pop_back();
        const int x = pt % p.n;
        const int y = pt / p.n;

        for (const auto& d : kOrtho)
        {
            const int nx = x + d[0];
            const int ny = y + d[1];
            if (!p.inBoard(nx, ny))
            {
                continue;
            }
            const int ni = p.idx(nx, ny);
            const uint8_t v = p.at(ni);
            if (v == 0)
            {
                if (!libSeen[ni])
                {
                    libSeen[ni] = 1;
                    ++libs;
                }
            }
            else if (v == stone && !seen[ni])
            {
                seen[ni] = 1;
                stack.push_back(ni);
            }
        }
    }
    return libs;
}

int GoAi::removeGroup(FastPos& p, int start, uint8_t stone, std::vector<int>* out)
{
    const int total = p.n * p.n;
    std::vector<uint8_t> seen(total, 0);
    std::vector<int>     stack;

    stack.push_back(start);
    seen[start] = 1;

    int removed = 0;
    while (!stack.empty())
    {
        const int pt = stack.back();
        stack.pop_back();
        p.cells[static_cast<size_t>(pt)] = 0;
        ++removed;
        if (out)
        {
            out->push_back(pt);
        }

        const int x = pt % p.n;
        const int y = pt / p.n;
        for (const auto& d : kOrtho)
        {
            const int nx = x + d[0];
            const int ny = y + d[1];
            if (!p.inBoard(nx, ny))
            {
                continue;
            }
            const int ni = p.idx(nx, ny);
            if (p.at(ni) == stone && !seen[ni])
            {
                seen[ni] = 1;
                stack.push_back(ni);
            }
        }
    }
    return removed;
}

bool GoAi::isEye(const FastPos& p, int point, uint8_t stone)
{
    if (p.at(point) != 0)
    {
        return false;
    }

    const int x = point % p.n;
    const int y = point / p.n;

    for (const auto& d : kOrtho)
    {
        const int nx = x + d[0];
        const int ny = y + d[1];
        if (!p.inBoard(nx, ny))
        {
            continue;
        }
        if (p.at(p.idx(nx, ny)) != stone)
        {
            return false;
        }
    }

    const bool onEdge = (x == 0 || y == 0 || x == p.n - 1 || y == p.n - 1);
    int        own    = 0;
    int        opp    = 0;
    for (const auto& d : kDiag)
    {
        const int nx = x + d[0];
        const int ny = y + d[1];
        if (!p.inBoard(nx, ny))
        {
            continue;
        }
        const uint8_t v = p.at(p.idx(nx, ny));
        if (v == stone)
        {
            ++own;
        }
        else if (v != 0)
        {
            ++opp;
        }
    }

    const int need = onEdge ? 1 : 2;
    return own >= need && opp == 0;
}

bool GoAi::isLegalMove(const FastPos& p, int point)
{
    if (point < 0 || p.at(point) != 0)
    {
        return false;
    }
    if (point == p.koPoint)
    {
        return false;
    }

    FastPos t = p;
    const uint8_t me  = p.toMove;
    const uint8_t opp = (me == 1) ? 2 : 1;

    t.cells[static_cast<size_t>(point)] = me;

    const int x = point % p.n;
    const int y = point / p.n;
    for (const auto& d : kOrtho)
    {
        const int nx = x + d[0];
        const int ny = y + d[1];
        if (!p.inBoard(nx, ny))
        {
            continue;
        }
        const int ni = p.idx(nx, ny);
        if (t.at(ni) == opp && countLibs(t, ni, opp) == 0)
        {
            removeGroup(t, ni, opp, nullptr);
        }
    }

    return countLibs(t, point, me) > 0;
}

bool GoAi::tryPlay(FastPos& p, int point, PlayRec* rec)
{
    PlayRec local;
    PlayRec& r = rec ? *rec : local;

    r.point        = point;
    r.player       = p.toMove;
    r.koBefore     = p.koPoint;
    r.passesBefore = p.passes;
    r.removed.clear();
    r.removedColor.clear();

    const uint8_t me  = p.toMove;
    const uint8_t opp = (me == 1) ? 2 : 1;

    if (point < 0)
    {
        // pass
        ++p.passes;
        p.toMove  = opp;
        p.koPoint = -1;
        return true;
    }

    if (point >= p.n * p.n || p.at(point) != 0)
    {
        return false;
    }
    if (point == p.koPoint)
    {
        return false;
    }

    p.cells[static_cast<size_t>(point)] = me;

    const int x = point % p.n;
    const int y = point / p.n;

    int removedGroups   = 0;
    int lastGroupSize   = 0;
    int lastGroupSeat   = -1;

    for (const auto& d : kOrtho)
    {
        const int nx = x + d[0];
        const int ny = y + d[1];
        if (!p.inBoard(nx, ny))
        {
            continue;
        }
        const int ni = p.idx(nx, ny);
        if (p.at(ni) == opp && countLibs(p, ni, opp) == 0)
        {
            std::vector<int> removed;
            const int        n = removeGroup(p, ni, opp, &removed);
            ++removedGroups;
            lastGroupSize = n;
            lastGroupSeat = ni;
            for (int q : removed)
            {
                r.removed.push_back(q);
                r.removedColor.push_back(opp);
            }
        }
    }

    if (countLibs(p, point, me) == 0)
    {
        // 自杀：回滚
        for (size_t i = 0; i < r.removed.size(); ++i)
        {
            p.cells[static_cast<size_t>(r.removed[i])] = r.removedColor[i];
        }
        p.cells[static_cast<size_t>(point)] = 0;
        return false;
    }

    // 简单劫
    p.koPoint = -1;
    if (removedGroups == 1 && lastGroupSize == 1 && countLibs(p, point, me) == 1)
    {
        p.koPoint = lastGroupSeat;
    }

    p.passes = 0;
    p.toMove = opp;
    return true;
}

void GoAi::undoPlay(FastPos& p, const PlayRec& r)
{
    if (r.point >= 0)
    {
        p.cells[static_cast<size_t>(r.point)] = 0;
    }
    for (size_t i = 0; i < r.removed.size(); ++i)
    {
        p.cells[static_cast<size_t>(r.removed[i])] = r.removedColor[i];
    }
    p.koPoint = r.koBefore;
    p.passes  = r.passesBefore;
    p.toMove  = r.player;
}

// ---------------------------------------------------------------------------
//  数子（中国规则，不含贴目）
// ---------------------------------------------------------------------------

int GoAi::areaScoreBlackMinusWhite(const FastPos& p)
{
    int black = 0;
    int white = 0;

    for (uint8_t v : p.cells)
    {
        if (v == 1) { ++black; }
        else if (v == 2) { ++white; }
    }

    const int total = p.n * p.n;
    std::vector<uint8_t> seen(total, 0);

    for (int i = 0; i < total; ++i)
    {
        if (p.cells[static_cast<size_t>(i)] != 0 || seen[static_cast<size_t>(i)])
        {
            continue;
        }

        std::vector<int> stack;
        stack.push_back(i);
        seen[static_cast<size_t>(i)] = 1;

        int  size         = 0;
        bool touchBlack   = false;
        bool touchWhite   = false;

        while (!stack.empty())
        {
            const int pt = stack.back();
            stack.pop_back();
            ++size;

            const int x = pt % p.n;
            const int y = pt / p.n;
            for (const auto& d : kOrtho)
            {
                const int nx = x + d[0];
                const int ny = y + d[1];
                if (!p.inBoard(nx, ny))
                {
                    continue;
                }
                const int ni = p.idx(nx, ny);
                const uint8_t v = p.cells[static_cast<size_t>(ni)];
                if (v == 1) { touchBlack = true; }
                else if (v == 2) { touchWhite = true; }
                else if (!seen[static_cast<size_t>(ni)])
                {
                    seen[static_cast<size_t>(ni)] = 1;
                    stack.push_back(ni);
                }
            }
        }

        if (touchBlack && !touchWhite) { black += size; }
        else if (touchWhite && !touchBlack) { white += size; }
    }

    return black - white;
}

bool GoAi::blackWinsByArea(const FastPos& p, double komi)
{
    const double diff = static_cast<double>(areaScoreBlackMinusWhite(p)) - komi;
    return diff > 0.0;
}

// ---------------------------------------------------------------------------
//  随机对局（playout）
// ---------------------------------------------------------------------------
//  纯随机在围棋里质量很差，所以加三条轻量启发式：
//    1) 能提子就先提
//    2) 自己被打吃（1 气）先跑
//    3) 不填自己的眼
//  这三条只做"局部扫描"，不做整盘搜索，成本很低但显著提升模拟质量。
int GoAi::playout(FastPos& p, int maxMoves, std::vector<PlayRec>& hist,
                  const std::atomic<bool>* cancel, double playoutDeadlineMs)
{
    const int total = p.n * p.n;
    int       passes = 0;
    int       played = 0;

    for (int move = 0; move < maxMoves && passes < 2 && !p.cells.empty(); ++move)
    {
        // 每 16 手检查一次取消/超时，兼顾开销与响应性。
        // 时间基准用 clock_（本次搜索开始时重置过），不能另起一个静态计时器 ——
        // 静态计时器是进程累计时间，与这里的**相对**预算比较会立刻超时，
        // 导致每次模拟都在头几手被截断、MCTS 退化成随机。
        if ((move & 15) == 0)
        {
            if (cancel && cancel->load(std::memory_order_relaxed))
            {
                return played;
            }
            if (playoutDeadlineMs > 0.0 && clock_.ms() >= playoutDeadlineMs)
            {
                return played;
            }
        }

        const uint8_t me  = p.toMove;
        const uint8_t opp = (me == 1) ? 2 : 1;

        // ---- 1) 先看能不能提子 ----
        int chosen = -1;
        for (int i = 0; i < total && chosen < 0; ++i)
        {
            if (p.cells[static_cast<size_t>(i)] != 0 || i == p.koPoint)
            {
                continue;
            }

            FastPos t = p;
            const int x = i % p.n;
            const int y = i / p.n;
            t.cells[static_cast<size_t>(i)] = me;

            int captured = 0;
            for (const auto& d : kOrtho)
            {
                const int nx = x + d[0];
                const int ny = y + d[1];
                if (!p.inBoard(nx, ny))
                {
                    continue;
                }
                const int ni = p.idx(nx, ny);
                if (t.at(ni) == opp && countLibs(t, ni, opp) == 0)
                {
                    captured += removeGroup(t, ni, opp, nullptr);
                }
            }
            if (captured > 0 && countLibs(t, i, me) > 0)
            {
                chosen = i;
            }
        }

        // ---- 2) 自己被打吃就先跑（在被打吃的块旁边找一口气） ----
        if (chosen < 0)
        {
            for (int i = 0; i < total && chosen < 0; ++i)
            {
                if (p.cells[static_cast<size_t>(i)] != me)
                {
                    continue;
                }
                if (countLibs(p, i, me) != 1)
                {
                    continue;
                }
                // 找这块的一个气点
                const int x = i % p.n;
                const int y = i / p.n;
                for (const auto& d : kOrtho)
                {
                    const int nx = x + d[0];
                    const int ny = y + d[1];
                    if (!p.inBoard(nx, ny))
                    {
                        continue;
                    }
                    const int ni = p.idx(nx, ny);
                    if (p.at(ni) == 0 && isLegalMove(p, ni))
                    {
                        chosen = ni;
                        break;
                    }
                }
            }
        }

        // ---- 3) 否则随机，避开自填眼与非法点 ----
        if (chosen < 0)
        {
            const int start = static_cast<int>(rng_.below(static_cast<uint32_t>(total)));
            for (int k = 0; k < total; ++k)
            {
                const int i = (start + k) % total;
                if (p.cells[static_cast<size_t>(i)] != 0)
                {
                    continue;
                }
                if (isEye(p, i, me))
                {
                    continue;   // 不填自己的眼
                }
                if (isLegalMove(p, i))
                {
                    chosen = i;
                    break;
                }
            }
        }

        PlayRec rec;
        if (chosen >= 0 && tryPlay(p, chosen, &rec))
        {
            hist.push_back(rec);
            passes = 0;
            ++played;
        }
        else
        {
            // 无处可下 -> pass
            PlayRec pr;
            tryPlay(p, -1, &pr);
            hist.push_back(pr);
            ++passes;
            ++played;
        }
    }

    return played;
}

// ---------------------------------------------------------------------------
//  L1 / L2 启发式选点
// ---------------------------------------------------------------------------

int GoAi::heuristicPick(const FastPos& p, int level, std::vector<int>& outOrder)
{
    const int total = p.n * p.n;
    const uint8_t me  = p.toMove;
    const uint8_t opp = (me == 1) ? 2 : 1;

    std::vector<int> candidates;
    for (int i = 0; i < total; ++i)
    {
        if (p.cells[static_cast<size_t>(i)] != 0)
        {
            continue;
        }
        if (isEye(p, i, me))
        {
            continue;
        }
        if (isLegalMove(p, i))
        {
            candidates.push_back(i);
        }
    }

    if (candidates.empty())
    {
        return -1;   // pass
    }

    if (level <= 1)
    {
        outOrder = candidates;
        return candidates[static_cast<size_t>(
            rng_.below(static_cast<uint32_t>(candidates.size())))];
    }

    // L2：给每个候选打分
    int best      = candidates[0];
    int bestScore = -1 << 20;

    for (int c : candidates)
    {
        int score = 0;

        FastPos t = p;
        const int x = c % p.n;
        const int y = c / p.n;
        t.cells[static_cast<size_t>(c)] = me;

        // 提子
        int captured = 0;
        for (const auto& d : kOrtho)
        {
            const int nx = x + d[0];
            const int ny = y + d[1];
            if (!p.inBoard(nx, ny))
            {
                continue;
            }
            const int ni = p.idx(nx, ny);
            if (t.at(ni) == opp && countLibs(t, ni, opp) == 0)
            {
                captured += removeGroup(t, ni, opp, nullptr);
            }
        }
        if (captured > 0)
        {
            score += captured * 120;
        }

        const int myLibs = countLibs(t, c, me);
        if (myLibs == 1)
        {
            score -= 150;   // 自陷打吃，通常很坏
        }
        else if (myLibs >= 3)
        {
            score += 12;
        }

        // 打吃对方（让对手某块变成 1 气）
        for (const auto& d : kOrtho)
        {
            const int nx = x + d[0];
            const int ny = y + d[1];
            if (!p.inBoard(nx, ny))
            {
                continue;
            }
            const int ni = p.idx(nx, ny);
            if (t.at(ni) == opp && countLibs(t, ni, opp) == 1)
            {
                score += 60;
            }
            // 救自己的 1 气块
            if (t.at(ni) == me && countLibs(t, ni, me) >= 2 &&
                countLibs(p, ni, me) == 1)
            {
                score += 70;
            }
        }

        // 靠着自己的子走（布局阶段有意义）
        int neighbors = 0;
        for (const auto& d : kOrtho)
        {
            const int nx = x + d[0];
            const int ny = y + d[1];
            if (!p.inBoard(nx, ny))
            {
                continue;
            }
            if (p.at(p.idx(nx, ny)) == me)
            {
                ++neighbors;
            }
        }
        score += neighbors * 6;

        // 轻微偏好中腹与四路
        const int dist = std::min({x, y, p.n - 1 - x, p.n - 1 - y});
        if (dist == 2 || dist == 3)
        {
            score += 8;
        }
        if (dist == 0)
        {
            score -= 6;   // 一线价值低
        }

        if (score > bestScore)
        {
            bestScore = score;
            best      = c;
        }
    }

    outOrder = candidates;
    return best;
}

// ---------------------------------------------------------------------------
//  对外入口
// ---------------------------------------------------------------------------

AiOutcome GoAi::search(const IGame& position, int level, int timeBudgetMs,
                       const std::atomic<bool>& cancel, AiProgress* progress)
{
    AiOutcome out;

    const auto* game = dynamic_cast<const GoGame*>(&position);
    if (!game || game->isOver())
    {
        out.hint.complete = true;
        return out;
    }

    level = std::clamp(level, 1, 5);

    // 计时器与截止时间用同一个钟表达：clock_ 每次搜索重置，
    // deadlineMs_ 是本次搜索的相对预算。
    clock_.reset();
    deadlineMs_ = (timeBudgetMs > 0) ? timeBudgetMs : 800;

    // ---- 把局面搬到 FastPos ----
    FastPos root;
    root.reset(game->cols());
    for (int y = 0; y < game->rows(); ++y)
    {
        for (int x = 0; x < game->cols(); ++x)
        {
            root.cells[static_cast<size_t>(root.idx(x, y))] = game->at(x, y);
        }
    }
    root.toMove = (game->sideToMove() == Side::First) ? 1 : 2;

    // 打劫禁着点也必须同步。漏掉它会让 AI 选出"游戏判定为非法"的着法
    // （表现是偶尔冒出一手被拒绝、或提示里出现非法推荐点）。
    if (game->hasKoPoint())
    {
        const Coord k = game->koPoint();
        if (k.valid())
        {
            root.koPoint = root.idx(k.x, k.y);
        }
    }

    const int total = root.n * root.n;

    // ---- L1 / L2：启发式 ----
    if (level <= 2)
    {
        std::vector<int> order;
        const int        pick = heuristicPick(root, level, order);

        if (pick >= 0)
        {
            const Move m = [&] {
                Move mm{};
                mm.to = Coord{pick % root.n, pick / root.n};
                return mm;
            }();
            out.move    = m;
            out.hasMove = true;
        }
        else
        {
            out.move    = Move{};   // pass
            out.hasMove = true;
        }

        out.hint.complete   = true;
        out.hint.thinkingMs = static_cast<int>(clock_.ms());

        if (pick >= 0)
        {
            HintCell rec;
            rec.coord  = Coord{pick % root.n, pick / root.n};
            rec.kind   = HintKind::Recommended;
            rec.weight = 1.0f;
            out.hint.cells.push_back(rec);
        }

        out.hint.notes.push_back(level == 1 ? "入门：随机合法着点（避开自填眼）"
                                            : "初级：一步启发式（提子 / 逃跑 / 避免自陷打吃）");
        if (progress)
        {
            progress->depth.store(1);
        }
        return out;
    }

    // ---- L3+：MCTS ----
    int budget = 800;
    switch (level)
    {
    case 3: budget = 800; break;
    case 4: budget = 3000; break;
    default: budget = 8000; break;
    }

    // 节点数组（含 pass 着法）
    const int maxChildren = total + 1;

    std::vector<Node> nodes;
    nodes.reserve(static_cast<size_t>(budget) * 2 + 16);

    Node rootNode;
    rootNode.move = -1;
    nodes.push_back(rootNode);

    const double komi = game->komi();
    int          playouts = 0;
    long long    playoutMoves = 0;   // 累计模拟手数，用于诊断 playout 是否被截断

    std::vector<PlayRec> hist;
    hist.reserve(static_cast<size_t>(total) * 2);

    std::vector<int> childMoves;   // 复用的着法列表

    while (playouts < budget)
    {
        if (cancel.load(std::memory_order_relaxed))
        {
            break;
        }
        if ((playouts & 31) == 0 && clock_.ms() >= deadlineMs_)
        {
            break;
        }
        ++playouts;

        // ---- 1) 选择：从根按 UCT 走到"还有未展开着法"的节点 ----
        FastPos              cur = root;
        std::vector<PlayRec> applied;
        std::vector<int>     path;

        int node = 0;
        // 一直往下走，直到遇见"还有 untried"的节点（或叶子）
        while (nodes[static_cast<size_t>(node)].untried.empty() &&
               nodes[static_cast<size_t>(node)].firstChild >= 0)
        {
            // 收集子节点，按 UCT 选一个
            int bestChild = -1;
            double bestVal = -1e18;
            const int parentVisits = std::max(1, nodes[static_cast<size_t>(node)].visits);

            for (int c = nodes[static_cast<size_t>(node)].firstChild; c >= 0;
                 c = nodes[static_cast<size_t>(c)].nextSibling)
            {
                const Node& ch = nodes[static_cast<size_t>(c)];
                const double uct =
                    ch.wins / std::max(1, ch.visits) +
                    1.414 * std::sqrt(std::log(static_cast<double>(parentVisits)) /
                                      std::max(1, ch.visits));
                if (uct > bestVal)
                {
                    bestVal   = uct;
                    bestChild = c;
                }
            }

            if (bestChild < 0)
            {
                break;
            }

            PlayRec rec;
            if (!tryPlay(cur, nodes[static_cast<size_t>(bestChild)].move, &rec))
            {
                break;
            }
            applied.push_back(rec);
            path.push_back(bestChild);
            node = bestChild;
        }

        // ---- 2) 扩展：生成候选（首次访问时），并取出一个加为新子节点 ----
        {
            Node& nd = nodes[static_cast<size_t>(node)];

            if (nd.untried.empty() && nd.firstChild < 0)
            {
                // 首次访问本节点：生成全部合法候选
                childMoves.clear();
                for (int i = 0; i < total; ++i)
                {
                    if (cur.cells[static_cast<size_t>(i)] != 0)
                    {
                        continue;
                    }
                    if (isEye(cur, i, cur.toMove))
                    {
                        continue;
                    }
                    if (isLegalMove(cur, i))
                    {
                        childMoves.push_back(i);
                    }
                }

                // 候选太多时做个剪枝：只保留"靠近已有棋子"的点。
                // 空盘时保留全部，否则开局无处可走。
                bool anyStone = false;
                for (uint8_t v : cur.cells)
                {
                    if (v != 0)
                    {
                        anyStone = true;
                        break;
                    }
                }
                if (anyStone && static_cast<int>(childMoves.size()) > 60)
                {
                    std::vector<int> filtered;
                    filtered.reserve(childMoves.size());
                    for (int i : childMoves)
                    {
                        const int x = i % cur.n;
                        const int y = i / cur.n;
                        bool      hasNeighbor = false;
                        for (int dy = -2; dy <= 2 && !hasNeighbor; ++dy)
                        {
                            for (int dx = -2; dx <= 2; ++dx)
                            {
                                const int nx = x + dx;
                                const int ny = y + dy;
                                if (!cur.inBoard(nx, ny))
                                {
                                    continue;
                                }
                                if (cur.at(cur.idx(nx, ny)) != 0)
                                {
                                    hasNeighbor = true;
                                    break;
                                }
                            }
                        }
                        if (hasNeighbor)
                        {
                            filtered.push_back(i);
                        }
                    }
                    if (!filtered.empty())
                    {
                        childMoves = std::move(filtered);
                    }
                }

                // 打乱，保证探索顺序不依赖生成顺序
                for (int i = static_cast<int>(childMoves.size()) - 1; i > 0; --i)
                {
                    const int j = static_cast<int>(
                        rng_.below(static_cast<uint32_t>(i + 1)));
                    std::swap(childMoves[static_cast<size_t>(i)],
                              childMoves[static_cast<size_t>(j)]);
                }

                nd.untried = std::move(childMoves);
            }

            if (!nd.untried.empty())
            {
                const int pickMove = nd.untried.back();
                nd.untried.pop_back();

                PlayRec rec;
                if (tryPlay(cur, pickMove, &rec))
                {
                    Node child;
                    child.move   = pickMove;
                    child.parent = node;

                    const int childIdx = static_cast<int>(nodes.size());
                    nodes.push_back(std::move(child));
                    nodes[static_cast<size_t>(childIdx)].nextSibling =
                        nodes[static_cast<size_t>(node)].firstChild;
                    nodes[static_cast<size_t>(node)].firstChild = childIdx;
                    ++nodes[static_cast<size_t>(node)].childCount;

                    applied.push_back(rec);
                    path.push_back(childIdx);
                    node = childIdx;
                }
            }
        }

        // ---- 3) 模拟 ----
        hist.clear();
        playoutMoves += playout(cur, total * 2, hist, &cancel, deadlineMs_);

        // ---- 4) 回传 ----
        //  约定：某个节点上累计的是"**走到该节点的那一手**的下方是否赢了"。
        //  路径第 0 个节点由 root.toMove 走出，第 1 个由对方走出，依次交替。
        const bool blackWins = blackWinsByArea(cur, komi);
        const uint8_t opp    = (root.toMove == 1) ? 2 : 1;

        for (size_t k = 0; k < path.size(); ++k)
        {
            Node&         nd    = nodes[static_cast<size_t>(path[k])];
            const uint8_t mover = (k % 2 == 0) ? root.toMove : opp;

            ++nd.visits;
            if (mover == 1 ? blackWins : !blackWins)
            {
                nd.wins += 1.0;
            }
        }
        ++nodes[0].visits;

        if (progress && (playouts & 63) == 0)
        {
            progress->playouts.store(playouts);
            progress->nodes.store(static_cast<long long>(nodes.size()));
        }
    }

    if (progress)
    {
        progress->playouts.store(playouts);
        progress->nodes.store(static_cast<long long>(nodes.size()));
        progress->depth.store(1);
    }

    // ---- 选根的最佳子节点（访问次数最多） ----
    int bestChild = -1;
    int bestVisits = -1;
    for (int c = nodes[0].firstChild; c >= 0; c = nodes[static_cast<size_t>(c)].nextSibling)
    {
        if (nodes[static_cast<size_t>(c)].visits > bestVisits)
        {
            bestVisits = nodes[static_cast<size_t>(c)].visits;
            bestChild  = c;
        }
    }

    out.hint.complete   = !cancel.load(std::memory_order_relaxed);
    out.hint.thinkingMs = static_cast<int>(clock_.ms());
    out.hint.nodes      = static_cast<long long>(nodes.size());
    out.hint.depth      = 1;

    if (bestChild < 0)
    {
        // 没能建出子树（例如只能 pass）
        out.move    = Move{};
        out.hasMove = true;
        out.hint.notes.push_back("蒙特卡洛没有找到有意义的着点，选择停一手");
        return out;
    }

    const Node& best = nodes[static_cast<size_t>(bestChild)];
    out.move    = Move{};
    out.move.to = (best.move < 0) ? Coord{} : Coord{best.move % root.n, best.move / root.n};
    out.hasMove = true;

    // ---- 胜率（从当前行棋方视角）----
    const double winRate = nodes[static_cast<size_t>(bestChild)].visits > 0
                               ? best.wins / best.visits
                               : 0.5;

    out.hint.hasWinRate = true;
    out.hint.winRate    = std::clamp(winRate, 0.0, 1.0);

    out.hint.hasEval = true;
    out.hint.evalCp  = static_cast<int>((winRate - 0.5) * 2000.0);
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "胜率 %.1f%%", winRate * 100.0);
        out.hint.evalText = buf;
    }

    if (best.move >= 0)
    {
        HintCell rec;
        rec.coord  = Coord{best.move % root.n, best.move / root.n};
        rec.kind   = HintKind::Recommended;
        rec.weight = 1.0f;
        out.hint.cells.push_back(rec);

        // 与最佳手竞争的其他候选，用 weight 体现"差距"
        for (int c = nodes[0].firstChild; c >= 0; c = nodes[static_cast<size_t>(c)].nextSibling)
        {
            if (c == bestChild)
            {
                continue;
            }
            const Node& ch = nodes[static_cast<size_t>(c)];
            if (ch.visits <= 0 || ch.move < 0)
            {
                continue;
            }
            const double wr = ch.wins / ch.visits;
            if (wr < winRate - 0.25)
            {
                continue;
            }
            HintCell hc;
            hc.coord  = Coord{ch.move % root.n, ch.move / root.n};
            hc.kind   = HintKind::Good;
            hc.weight = static_cast<float>(std::clamp(wr / std::max(1e-6, winRate), 0.0, 1.0));
            out.hint.cells.push_back(hc);
        }
    }

    // ---- 打劫提示（L5）----
    if (level >= 5 && game->hasKoPoint())
    {
        out.hint.notes.push_back("盘面存在打劫禁着点");
    }
    if (game->consecutivePasses() == 1)
    {
        out.hint.notes.push_back("对方已停一手：若局面已定可停手终局，否则继续收官");
    }

    {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "MCTS %d 次模拟，平均每局 %d 手，%lld 节点，用时 %d ms", playouts,
                      playouts > 0 ? static_cast<int>(playoutMoves / playouts) : 0,
                      out.hint.nodes, out.hint.thinkingMs);
        out.hint.notes.push_back(buf);
    }

    out.hint.mctsPlayouts      = playouts;
    out.hint.mctsAvgPlayoutLen = playouts > 0 ? static_cast<int>(playoutMoves / playouts) : 0;

    return out;
}

} // namespace chess
