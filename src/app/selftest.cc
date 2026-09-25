#include "app/selftest.h"

#include "ai/go_ai.h"
#include "ai/gomoku_ai.h"
#include "ai/xiangqi_ai.h"
#include "app/app.h"
#include "app/atlas_scene.h"
#include "app/menu_scene.h"
#include "core/config.h"
#include "core/rng.h"
#include "games/flip_puzzle.h"
#include "games/go.h"
#include "games/gomoku.h"
#include "games/gomoku_eval.h"
#include "games/registry.h"
#include "games/xiangqi.h"
#include "ui/atlas.h"
#include "ui/board_view.h"
#include "ui/gfx_util.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace chess {

namespace {

std::string hexColor(color_t c)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(c));
    return buf;
}

} // namespace

int gfxSelfTest()
{
    constexpr int W = 64;
    constexpr int H = 64;

    PIMAGE img = newimage(W, H);
    if (!img)
    {
        std::printf("[gfx] newimage failed\n");
        return 1;
    }

    // 1) 背景：cleardevice 直接写像素缓冲，alpha 正确
    setbkcolor(EGERGB(0xF0, 0xF0, 0xF0), img);
    cleardevice(img);

    // 2) bar：走 GDI FillRect + DC 当前画刷
    const color_t RED   = EGERGB(0xFF, 0x00, 0x00);
    const color_t GREEN = EGERGB(0x00, 0xFF, 0x00);
    setfillcolor(RED, img);
    bar(0, 0, 31, 31, img);
    setfillcolor(GREEN, img);
    bar(32, 0, 63, 31, img);

    // 3) 线：GDI 画笔
    const color_t BLUE = EGERGB(0x00, 0x00, 0xFF);
    setcolor(BLUE, img);
    rectangle(34, 34, 61, 61, img);

    // 4) GDI+ 路径
    const color_t YELLOW = EGERGB(0xFF, 0xFF, 0x00);
    setfillcolor(YELLOW, img);
    fillellipse(48, 48, 12, 12, img);

    // ---- 采样 ----
    struct Sample
    {
        const char* what;
        int         x, y;
        color_t     expect;
    };
    const Sample samples[] = {
        // (1,63) 落在所有图元之外，才是真正的 cleardevice 背景色
        {"cleardevice bkcolor   ", 1, 63, EGERGB(0xF0, 0xF0, 0xF0)},
        {"bar setfillcolor RED  ", 10, 10, RED},
        {"bar setfillcolor GREEN", 50, 10, GREEN},
        {"fillellipse YELLOW    ", 48, 48, YELLOW},
    };

    int bad = 0;
    std::printf("[gfx] offscreen %dx%d\n", W, H);
    for (const Sample& s : samples)
    {
        const color_t got = getpixel(s.x, s.y, img);
        const bool    ok  = (got & 0x00FFFFFF) == (s.expect & 0x00FFFFFF);
        if (!ok)
        {
            ++bad;
        }
        std::printf("[gfx] %s at(%2d,%2d) expect=%s got=%s alpha=0x%02X %s\n",
                    s.what, s.x, s.y, hexColor(s.expect).c_str(), hexColor(got).c_str(),
                    static_cast<unsigned>(got >> 24), ok ? "ok" : "MISMATCH");
    }

    // ---- alpha 直方图：能直观看出哪些绘制路径没写 alpha ----
    int alphaZero = 0, alphaFull = 0, alphaPartial = 0;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const unsigned a = static_cast<unsigned>(getpixel_f(x, y, img)) >> 24;
            if (a == 0) { ++alphaZero; }
            else if (a == 0xFF) { ++alphaFull; }
            else { ++alphaPartial; }
        }
    }
    std::printf("[gfx] alpha histogram: zero=%d full=%d partial=%d\n",
                alphaZero, alphaFull, alphaPartial);

    // ---- 保存对比：不补 alpha vs 补齐 alpha ----
    const bool rawOk = savepng(img, "build/gfx_selftest_raw.png", false) == grOk;
    makeImageOpaque(img);
    const bool fixOk = savepng(img, "build/gfx_selftest_fixed.png", false) == grOk;
    std::printf("[gfx] saved raw=%d fixed=%d  (raw 应出现大片纯黑)\n",
                rawOk ? 1 : 0, fixOk ? 1 : 0);

    // ---- rectangle 的边线 ----
    int blueCount = 0;
    for (int x = 30; x <= 63; ++x)
    {
        for (int y = 30; y <= 63; ++y)
        {
            if ((getpixel(x, y, img) & 0x00FFFFFF) == (BLUE & 0x00FFFFFF))
            {
                ++blueCount;
            }
        }
    }
    std::printf("[gfx] rectangle BLUE pixels in 30..63 box = %d  %s\n",
                blueCount, blueCount > 0 ? "ok" : "MISMATCH");
    if (blueCount == 0)
    {
        ++bad;
    }

    delimage(img);
    std::printf("[gfx] %s (%d mismatches)\n",
                bad == 0 ? "ALL OK" : "HAS MISMATCHES", bad);
    return bad == 0 ? 0 : 1;
}

int atlasDump()
{
    Atlas& atlas = App::inst().atlas();

    std::printf("[atlas] res/chesses100.png  %dx%d  unitPixels=%d  sprites=%d\n",
                atlas.width(), atlas.height(), Atlas::kUnitPixels,
                static_cast<int>(atlas.names().size()));
    std::printf("[atlas] %-14s %6s %6s %6s %6s\n", "name", "x", "y", "w", "h");
    for (const auto& n : atlas.names())
    {
        const SpriteRect r = atlas.rect(n);
        std::printf("[atlas] %-14s %6d %6d %6d %6d\n", n.c_str(), r.x, r.y, r.w, r.h);
    }

    const char* out = "build/atlas_dump.png";
    const bool  ok  = dumpAtlasSheet(atlas, out);
    std::printf("[atlas] contact sheet: %s\n", ok ? out : "FAILED");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  翻转棋求解器自检
// ---------------------------------------------------------------------------

namespace {

using chess::flip::Cell;

// 暴力枚举所有按法，求"把 board 变成全同色"的最少按键数。
// 只在 n <= 16 时可用（2^16 已经足够验证了）。
int bruteForceMin(const std::vector<Cell>& board, int cols, int rows, bool& solvable)
{
    const int n = cols * rows;
    if (n > 16)
    {
        solvable = false;
        return -1;
    }

    int best = -1;
    for (uint32_t mask = 0; mask < (1u << n); ++mask)
    {
        std::vector<Cell> tmp = board;
        int               count = 0;
        for (int i = 0; i < n; ++i)
        {
            if (mask & (1u << i))
            {
                chess::flip::applyPress(tmp, cols, rows, i % cols, i / cols);
                ++count;
            }
        }

        if (chess::flip::potential(tmp) != 0)
        {
            continue;
        }
        if (best < 0 || count < best)
        {
            best = count;
        }
    }

    solvable = (best >= 0);
    return best;
}

// 按求解器给的方案走一遍，检查是否真的全同色
bool applySolution(const std::vector<Cell>& board, int cols, int rows,
                   const chess::flip::Solution& sol)
{
    if (!sol.found)
    {
        return false;
    }
    std::vector<Cell> tmp = board;
    int               used = 0;
    for (int i = 0; i < cols * rows; ++i)
    {
        if (sol.press[i])
        {
            chess::flip::applyPress(tmp, cols, rows, i % cols, i / cols);
            ++used;
        }
    }
    return chess::flip::potential(tmp) == 0 && used == sol.pressCount;
}

} // namespace

int flipSolverTest()
{
    int failures = 0;
    chess::Rng rng(0xC0FFEEull);

    // ---- 1) 3x3：对全部 512 个局面穷举比对 ----
    {
        const int cols = 3, rows = 3, n = 9;
        int tested = 0, compared = 0, mismatch = 0, solvableBySolver = 0;

        for (uint32_t mask = 0; mask < (1u << n); ++mask)
        {
            std::vector<Cell> board(n);
            for (int i = 0; i < n; ++i)
            {
                board[i] = (mask & (1u << i)) ? chess::flip::kWhite : chess::flip::kBlack;
            }
            ++tested;

            const auto sol = chess::flip::solveAny(board, cols, rows);
            if (!applySolution(board, cols, rows, sol))
            {
                ++failures;
                std::printf("[flip] 3x3 mask=%u: solution does not solve the board\n", mask);
                continue;
            }
            ++solvableBySolver;

            bool bruteSolvable = false;
            const int bruteMin = bruteForceMin(board, cols, rows, bruteSolvable);
            if (bruteSolvable)
            {
                ++compared;
                if (bruteMin != sol.pressCount)
                {
                    ++mismatch;
                    if (mismatch <= 5)
                    {
                        std::printf("[flip] 3x3 mask=%u: solver=%d brute=%d MISMATCH\n",
                                    mask, sol.pressCount, bruteMin);
                    }
                }
            }
        }

        std::printf("[flip] 3x3 exhaustive: boards=%d solvable=%d compared=%d minMismatch=%d\n",
                    tested, solvableBySolver, compared, mismatch);
        failures += mismatch;
    }

    // ---- 2) 4x4：按游戏的方式生成局面，与暴力枚举比对最少步数 ----
    //
    //  注意这里刻意用"从全白按若干次"生成，而不是随机撒棋子：
    //  4x4 的覆盖矩阵奇异（秩 12 < 16），任意随机局面里大多数是**无解**的。
    //  游戏只会产生"从全同色可达"的局面，所以测试也必须按同样的方式构造。
    {
        const int cols = 4, rows = 4, n = 16;
        int tested = 0, compared = 0, mismatch = 0, unsolvable = 0;

        for (int t = 0; t < 300; ++t)
        {
            std::vector<Cell> board(n, chess::flip::kWhite);
            const int presses = 3 + static_cast<int>(rng.below(6));
            for (int i = 0; i < presses; ++i)
            {
                chess::flip::applyPress(board, cols, rows,
                                        static_cast<int>(rng.below(cols)),
                                        static_cast<int>(rng.below(rows)));
            }
            ++tested;

            const auto sol = chess::flip::solveAny(board, cols, rows);
            if (!sol.found)
            {
                ++unsolvable;
                std::printf("[flip] 4x4 trial=%d: scrambled board reported unsolvable "
                            "(should be impossible)\n", t);
                ++failures;
                continue;
            }
            if (!applySolution(board, cols, rows, sol))
            {
                ++failures;
                std::printf("[flip] 4x4 trial=%d: solution does not solve the board\n", t);
                continue;
            }

            bool      bruteSolvable = false;
            const int bruteMin      = bruteForceMin(board, cols, rows, bruteSolvable);
            if (bruteSolvable)
            {
                ++compared;
                if (bruteMin != sol.pressCount)
                {
                    ++mismatch;
                    if (mismatch <= 5)
                    {
                        std::printf("[flip] 4x4 trial=%d: solver=%d brute=%d MISMATCH\n",
                                    t, sol.pressCount, bruteMin);
                    }
                }
            }
        }

        std::printf("[flip] 4x4 scrambled: boards=%d compared=%d minMismatch=%d unsolvable=%d\n",
                    tested, compared, mismatch, unsolvable);
        failures += mismatch;
    }

    // ---- 2b) 说明性检查：4x4 的任意随机局面大多无解 ----
    //  这条不是断言，而是把这个数学事实记录下来：
    //  它是"为什么必须用打乱生成、不能随机撒棋子"的依据。
    {
        const int cols = 4, rows = 4, n = 16;
        int       solvableCount = 0;
        const int trials = 2000;

        for (int t = 0; t < trials; ++t)
        {
            std::vector<Cell> board(n);
            for (int i = 0; i < n; ++i)
            {
                board[i] = static_cast<Cell>(rng.below(2));
            }
            if (chess::flip::solveAny(board, cols, rows).found)
            {
                ++solvableCount;
            }
        }
        std::printf("[flip] 4x4 arbitrary boards: solvable %d/%d (%.1f%%) "
                    "-> must scramble, cannot scatter\n",
                    solvableCount, trials, 100.0 * solvableCount / trials);
    }

    // ---- 3) 5x5 / 8x8 / 9x9：同样按游戏方式生成，验证方案有效 ----
    for (const int size : {5, 8, 9})
    {
        const int n = size * size;
        int       tested = 0, solved = 0, unsolvable = 0;

        for (int t = 0; t < 200; ++t)
        {
            std::vector<Cell> board(n, chess::flip::kWhite);
            const int presses = 3 + static_cast<int>(rng.below(static_cast<uint32_t>(n / 3 + 1)));
            for (int i = 0; i < presses; ++i)
            {
                chess::flip::applyPress(board, size, size,
                                        static_cast<int>(rng.below(static_cast<uint32_t>(size))),
                                        static_cast<int>(rng.below(static_cast<uint32_t>(size))));
            }
            ++tested;

            const auto sol = chess::flip::solveAny(board, size, size);
            if (!sol.found)
            {
                ++unsolvable;
                ++failures;
                std::printf("[flip] %dx%d trial=%d: scrambled board reported unsolvable\n",
                            size, size, t);
                continue;
            }
            if (!applySolution(board, size, size, sol))
            {
                ++failures;
                std::printf("[flip] %dx%d trial=%d: solution does not solve the board\n",
                            size, size, t);
                continue;
            }
            ++solved;
        }
        std::printf("[flip] %dx%d scrambled: boards=%d solved=%d unsolvable=%d\n",
                    size, size, tested, solved, unsolvable);
    }

    // ---- 4) required 标记：标记为"所有解都按"的格子必须在方案里为 1 ----
    {
        const int cols = 5, rows = 5, n = 25;
        int       sampled = 0, violations = 0;

        for (int t = 0; t < 200; ++t)
        {
            std::vector<Cell> board(n);
            for (int i = 0; i < n; ++i)
            {
                board[i] = static_cast<Cell>(rng.below(2));
            }
            const auto sol = chess::flip::solveAny(board, cols, rows);
            if (!sol.found)
            {
                continue;
            }
            ++sampled;

            for (int i = 0; i < n; ++i)
            {
                if (sol.required[i] && !sol.press[i])
                {
                    ++violations;
                }
            }
        }
        std::printf("[flip] 5x5 required-flag check: sampled=%d violations=%d\n",
                    sampled, violations);
        failures += violations;
    }

    std::printf("[flip] %s (%d failures)\n", failures == 0 ? "ALL OK" : "HAS FAILURES", failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  窗口 / 坐标自检
// ---------------------------------------------------------------------------

int windowInfoCheck()
{
    const int expectW = getwidth();
    const int expectH = getheight();

    // 先真正渲染若干帧：EGE 的客户区校正在 graphupdate() 里做，
    // 而它要等到绘图/延时被触发时才跑。不渲染就测量会读到建窗时的中间状态。
    for (int i = 0; i < 6; ++i)
    {
        setbkcolor(EGERGB(0x20, 0x20, 0x20));
        cleardevice();
        delay_fps(60);
    }

    // EGE 的窗口类名（见 ege_head.h 的 EGE_WNDCLSNAME）
    HWND hwnd = FindWindowW(L"Easy Graphics Engine", nullptr);
    if (!hwnd)
    {
        // 有些构建会把它作为子窗口创建，退一步找 EGE 的顶层窗口
        hwnd = GetActiveWindow();
    }
    if (!hwnd)
    {
        std::printf("[win] cannot locate the EGE window (FindWindow failed)\n");
        return 1;
    }

    RECT cr{};
    RECT wr{};
    GetClientRect(hwnd, &cr);
    GetWindowRect(hwnd, &wr);

    const int clientW = cr.right - cr.left;
    const int clientH = cr.bottom - cr.top;
    const int windowW = wr.right - wr.left;
    const int windowH = wr.bottom - wr.top;

    // 进程是否声明了 DPI 感知（EGE 自己不做，所以通常是不感知）
    HDC screenDc = GetDC(nullptr);
    const int dpiX = GetDeviceCaps(screenDc, LOGPIXELSX);
    const int dpiY = GetDeviceCaps(screenDc, LOGPIXELSY);
    ReleaseDC(nullptr, screenDc);

    HDC wndDc = GetDC(hwnd);
    const int wndDpiX = GetDeviceCaps(wndDc, LOGPIXELSX);
    ReleaseDC(hwnd, wndDc);

    std::printf("[win] EGE canvas      : %d x %d\n", expectW, expectH);
    std::printf("[win] window client   : %d x %d\n", clientW, clientH);
    std::printf("[win] window rect     : %d x %d (non-client %d x %d)\n",
                windowW, windowH, windowW - clientW, windowH - clientH);
    std::printf("[win] screen DPI      : %d x %d (scale %.2f)\n",
                dpiX, dpiY, dpiX / 96.0);
    std::printf("[win] window DPI      : %d\n", wndDpiX);
    std::printf("[win] SM_CXFRAME=%d SM_CYFRAME=%d SM_CYCAPTION=%d\n",
                GetSystemMetrics(SM_CXFRAME), GetSystemMetrics(SM_CYFRAME),
                GetSystemMetrics(SM_CYCAPTION));

    int failures = 0;

    if (clientW != expectW || clientH != expectH)
    {
        ++failures;
        std::printf("[win] MISMATCH: client area != canvas size\n");
        std::printf("[win]   -> click at canvas (x,y) but EGE buffer is %dx%d;\n",
                    expectW, expectH);
        std::printf("[win]      a click at client (cx,cy) maps to canvas (cx*%d/%d, cy*%d/%d)\n",
                    expectW, clientW, expectH, clientH);

        // 换算一下偏移有多大：以一个"点中某格"的典型坐标为例
        const int probeX = clientW / 2;
        const int probeY = clientH / 2;
        const int mappedX = probeX * expectW / clientW;
        const int mappedY = probeY * expectH / clientH;
        std::printf("[win]   center click: client=(%d,%d) -> canvas=(%d,%d) (delta %d,%d)\n",
                    probeX, probeY, mappedX, mappedY, mappedX - probeX, mappedY - probeY);
    }
    else
    {
        std::printf("[win] client area matches canvas size -> click coords are exact\n");
    }

    std::printf("[win] %s (%d failures)\n", failures == 0 ? "ALL OK" : "HAS MISMATCHES", failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  绘制 <-> 命中 一致性自检
// ---------------------------------------------------------------------------
//  这是最容易被忽略、也最容易"看起来只是有点偏"的不变量：
//  棋盘画在哪个像素位置，与 hitTest() 认为格子在哪，必须严格一致。
//  两者只要差一点点，手感就是"点不准"。
//
//  做法：把棋盘底 + 网格渲染到离屏图，按颜色扫描出真实包围盒，
//  再逐格检查"格子中心像素 hitTest 回来还是不是同一格"。
// ---------------------------------------------------------------------------

namespace {

// 扫出棋盘底色的包围盒；找不到返回 false
bool measureBoardBBox(PIMAGE img, Rect& out)
{
    const int w = getwidth(img);
    const int h = getheight(img);

    int minX = w, minY = h, maxX = -1, maxY = -1;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const color_t c = getpixel(x, y, img) & 0x00FFFFFFu;
            const color_t bg = chess::theme::kBoardBg & 0x00FFFFFFu;
            const color_t ed = chess::theme::kBoardEdge & 0x00FFFFFFu;
            if (c != bg && c != ed)
            {
                continue;
            }
            if (x < minX) { minX = x; }
            if (x > maxX) { maxX = x; }
            if (y < minY) { minY = y; }
            if (y > maxY) { maxY = y; }
        }
    }

    if (maxX < 0)
    {
        return false;
    }
    out = Rect{minX, minY, maxX - minX + 1, maxY - minY + 1};
    return true;
}

} // namespace

int hitTestCheck()
{
    int failures = 0;

    const Rect boardArea{0, chess::theme::kTopBarH,
                         chess::kWindowWidth - chess::theme::kHudW,
                         chess::kWindowHeight - chess::theme::kTopBarH};

    struct Case
    {
        const char*  label;
        BoardKind    kind;
        int          cols;
        int          rows;
    };
    const Case cases[] = {
        {"cellgrid 5x5", BoardKind::CellGrid, 5, 5},
        {"cellgrid 8x8", BoardKind::CellGrid, 8, 8},
        {"cellgrid 15x15", BoardKind::CellGrid, 15, 15},
        {"intersect 9x9", BoardKind::Intersections, 9, 9},        {"intersect 9x10", BoardKind::Intersections, 9, 10},
        {"intersect 19x19", BoardKind::Intersections, 19, 19},
    };

    for (const Case& cs : cases)
    {
        BoardSpec spec;
        spec.kind = cs.kind;
        spec.cols = cs.cols;
        spec.rows = cs.rows;

        BoardView bv;
        bv.layout(spec, boardArea);

        // 渲染到离屏图（只画底与网格，不用管 alpha，颜色比对只取低 24 位）
        PIMAGE img = newimage(chess::kWindowWidth, chess::kWindowHeight);
        if (!img)
        {
            std::printf("[hit] newimage failed\n");
            return 1;
        }
        setbkcolor(chess::theme::kBg, img);
        cleardevice(img);
        bv.drawBoardBase(img);
        bv.drawGrid(img);

        Rect measured{};
        if (!measureBoardBBox(img, measured))
        {
            std::printf("[hit] %-22s FAILED: no board pixels found\n", cs.label);
            ++failures;
            delimage(img);
            continue;
        }
        const Rect want = bv.boardRect();
        // 允许 1px 的取整/描边误差：这里要抓的是"偏了一格"这类真错误，
        // 不是亚像素级的边界差异（描边宽度与四舍五入都可能差 1px）。
        const bool bboxOk = (std::abs(measured.x - want.x) <= 1 &&
                             std::abs(measured.y - want.y) <= 1 &&
                             std::abs(measured.w - want.w) <= 2 &&
                             std::abs(measured.h - want.h) <= 2);
        if (!bboxOk)
        {
            std::printf("[hit] %-22s bbox mismatch: measured %d,%d %dx%d  layout %d,%d %dx%d\n",
                        cs.label, measured.x, measured.y, measured.w, measured.h,
                        want.x, want.y, want.w, want.h);
            ++failures;
        }

        // 逐格：格子中心必须命中自己
        int      cellErr    = 0;
        int      offsetErr  = 0;
        const int quarter   = std::max(1, bv.cell() / 4);

        for (int y = 0; y < cs.rows; ++y)
        {
            for (int x = 0; x < cs.cols; ++x)
            {
                const Coord c{x, y};
                const int   cx = bv.px(c);
                const int   cy = bv.py(c);

                if (bv.hitTest(cx, cy) != c)
                {
                    ++cellErr;
                }

                // 格子内偏移一点点（模拟手抖）也应命中同一格
                const int probes[4][2] = {
                    {cx - quarter, cy - quarter},
                    {cx + quarter, cy - quarter},
                    {cx - quarter, cy + quarter},
                    {cx + quarter, cy + quarter},
                };
                for (const auto& p : probes)
                {
                    if (bv.hitTest(p[0], p[1]) != c)
                    {
                        ++offsetErr;
                    }
                }
            }
        }

        const int totalProbes = cs.cols * cs.rows * 4;
        const bool geomOk = (cellErr == 0 && offsetErr == 0);
        if (!geomOk)
        {
            ++failures;
        }

        std::printf("[hit] %-18s bbox %-3s cell=%3d  centerErr=%d  offsetErr=%d/%d\n",
                    cs.label, bboxOk ? "ok" : "BAD", bv.cell(), cellErr, offsetErr,
                    totalProbes);

        delimage(img);
    }

    std::printf("[hit] %s (%d failures)\n", failures == 0 ? "ALL OK" : "HAS FAILURES",
                failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  五子棋自检
// ---------------------------------------------------------------------------

namespace {

using chess::gomoku::Board;
using chess::gomoku::Shape;

// 往棋盘上摆一串棋子（直接写 cell，便于构造测试局面）
void put(Board& b, int x, int y, int v) { b.set(x, y, static_cast<uint8_t>(v)); }

// 返回"落子后是否成五"
bool fiveIfPlace(const Board& b, int x, int y, int side)
{
    Board tmp = b;
    tmp.set(x, y, static_cast<uint8_t>(side));
    return chess::gomoku::makesFiveOrMore(tmp, x, y, side);
}

} // namespace

int gomokuTest()
{
    int failures = 0;
    auto expect = [&](bool cond, const char* what) {
        if (!cond)
        {
            ++failures;
            std::printf("[gomoku] FAIL: %s\n", what);
        }
        return cond;
    };

    // ---- 1) 胜负判定：4 个方向各测一次 ----
    {
        const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
        const char* names[4] = {"horizontal", "vertical", "diag-down", "diag-up"};

        for (int d = 0; d < 4; ++d)
        {
            Board b;
            b.reset(15, 15);
            const int cx = 7;
            const int cy = 7;

            // 在两侧各摆 2 枚，中心留空；放上中心即 5 连
            for (int k = 1; k <= 2; ++k)
            {
                put(b, cx + k * dirs[d][0], cy + k * dirs[d][1], 1);
                put(b, cx - k * dirs[d][0], cy - k * dirs[d][1], 1);
            }

            char msg[128];
            std::snprintf(msg, sizeof(msg), "%s: four in a row should become five", names[d]);
            expect(fiveIfPlace(b, cx, cy, 1), msg);

            // 只有 3 连时不应判胜
            Board c;
            c.reset(15, 15);
            for (int k = 1; k <= 2; ++k)
            {
                put(c, cx + k * dirs[d][0], cy + k * dirs[d][1], 1);
            }
            put(c, cx - 1 * dirs[d][0], cy - 1 * dirs[d][1], 1);
            // 此时放中心是第 5 枚（-1,0,+1,+2 共 4 枚 + 中心 = 5）—— 这其实是成五，
            // 所以这里改测"明确不够"的情形：只摆 3 枚
            Board e;
            e.reset(15, 15);
            put(e, cx + 1 * dirs[d][0], cy + 1 * dirs[d][1], 1);
            put(e, cx + 2 * dirs[d][0], cy + 2 * dirs[d][1], 1);
            std::snprintf(msg, sizeof(msg), "%s: three in a row should NOT be five", names[d]);
            expect(!fiveIfPlace(e, cx, cy, 1), msg);
        }
    }

    // ---- 2) 长连（>=6）也算胜（自由规则）----
    {
        Board b;
        b.reset(15, 15);
        for (int k = 2; k <= 5; ++k)
        {
            put(b, 3 + k, 7, 1);
        }
        put(b, 3, 7, 1);
        put(b, 9, 7, 1);
        // 8 枚连成一线，中间空 (4,7)? 不 —— 这里直接摆 6 枚并测
        Board c;
        c.reset(15, 15);
        for (int k = 0; k < 6; ++k)
        {
            put(c, 3 + k, 7, 1);
        }
        expect(chess::gomoku::makesFiveOrMore(c, 5, 7, 1), "overline (6) should count as win");
    }

    // ---- 3) 禁手判定 ----
    {
        // 3a) 长连 -> 禁手
        Board b;
        b.reset(15, 15);
        for (int k = 0; k < 5; ++k)
        {
            if (3 + k == 5)
            {
                continue;   // 留空中心
            }
            put(b, 3 + k, 7, 1);
        }
        // (5,7) 两侧共 4 枚 -> 落子成五，不算禁手
        expect(!chess::gomoku::checkForbidden(b, 5, 7).forbidden,
               "five (exactly) must NOT be forbidden");

        // 摆成 6 连：让 (5,7) 落子后成为 6
        Board d;
        d.reset(15, 15);
        put(d, 2, 7, 1);
        put(d, 3, 7, 1);
        put(d, 4, 7, 1);
        put(d, 6, 7, 1);
        put(d, 7, 7, 1);
        const auto info = chess::gomoku::checkForbidden(d, 5, 7);
        expect(info.overline, "placing to make 6 should be flagged as overline");
        expect(info.forbidden, "overline should be forbidden");
    }

    {
        // 3b) 双四：横向 + 纵向各成一个"四"
        Board b;
        b.reset(15, 15);
        put(b, 3, 5, 1);
        put(b, 4, 5, 1);
        put(b, 6, 5, 1);
        put(b, 5, 3, 1);
        put(b, 5, 4, 1);
        put(b, 5, 6, 1);
        const auto info = chess::gomoku::checkForbidden(b, 5, 5);
        expect(info.fourCount >= 2, "two crossing fours should give fourCount >= 2");
        expect(info.forbidden, "double-four should be forbidden");
    }

    {
        // 3c) 双活三：横向 + 纵向各成一个活三
        Board b;
        b.reset(15, 15);
        put(b, 4, 5, 1);
        put(b, 6, 5, 1);
        put(b, 5, 4, 1);
        put(b, 5, 6, 1);
        const auto info = chess::gomoku::checkForbidden(b, 5, 5);
        expect(info.openThreeCount >= 2, "two crossing open threes should give openThreeCount >= 2");
        expect(info.forbidden, "double-three should be forbidden");
    }

    {
        // 3d) 单个活三 + 单个活二 -> 不是禁手
        Board b;
        b.reset(15, 15);
        put(b, 4, 5, 1);
        put(b, 6, 5, 1);
        const auto info = chess::gomoku::checkForbidden(b, 5, 5);
        expect(!info.forbidden, "single open three should NOT be forbidden");
    }

    // ---- 4) 棋型识别 ----
    {
        // 4,5,6 三子已在，落 (3,7) 后形成 3-4-5-6 四子且两端(2,7)/(7,7)都空
        // -> 活四（不是冲四）
        Board b;
        b.reset(15, 15);
        put(b, 4, 7, 1);
        put(b, 5, 7, 1);
        put(b, 6, 7, 1);
        expect(chess::gomoku::shapeAt(b, 3, 7, 1, 0) == Shape::OpenFour,
               "placing at 3 gives an OPEN FOUR (both ends empty)");

        // 一端被堵 -> 冲四
        Board d;
        d.reset(15, 15);
        put(d, 2, 7, 2);   // 堵住左端
        put(d, 4, 7, 1);
        put(d, 5, 7, 1);
        put(d, 6, 7, 1);
        expect(chess::gomoku::shapeAt(d, 3, 7, 1, 0) == Shape::Four,
               "placing at 3 with left end blocked gives a FOUR");

        Board c;
        c.reset(15, 15);
        put(c, 4, 7, 1);
        put(c, 5, 7, 1);
        put(c, 6, 7, 1);
        put(c, 8, 7, 1);
        // 落 (7,7) 后是 4,5,6,7,8 = 五连
        expect(chess::gomoku::shapeAt(c, 7, 7, 1, 0) == Shape::Five,
               "gap-shape should still detect five");
    }

    // ---- 5) AI 行为 ----
    {
        // 5a) AI 有活四时必须去成五
        chess::GameConfig cfg;
        cfg.cols = 15;
        cfg.rows = 15;
        cfg.variant = 0;   // 自由规则

        chess::GomokuGame g;
        g.setup(cfg);

        const int seq[8][2] = {{7, 7}, {0, 0}, {8, 7}, {0, 5}, {9, 7}, {0, 10}, {10, 7}, {0, 14}};
        for (const auto& s : seq)
        {
            chess::Move m{};
            m.to = chess::Coord{s[0], s[1]};
            expect(g.apply(m), "setup move should be legal");
        }
        expect(g.sideToMove() == chess::Side::First, "black should be to move after 8 moves");

        chess::GomokuAi ai;
        std::atomic<bool> cancel{false};
        const auto out = ai.search(g, 3, 800, cancel, nullptr);
        expect(out.hasMove, "AI should return a move");

        Board after = g.board();
        after.set(out.move.to.x, out.move.to.y, 1);
        expect(chess::gomoku::makesFiveOrMore(after, out.move.to.x, out.move.to.y, 1),
               "AI must complete five when it has an open four");

        // 5b) 对手有**冲四**时必须去挡。
        //     注意不能用"活四"来测：活四两端都能成五，本来就挡不住，
        //     那种局面下 AI 无论走哪都是输，测"有没有挡"没有意义。
        //     这里在 (6,7) 先放一枚黑子堵住一端，只剩 (11,7) 一个成五点。
        chess::GomokuGame g2;
        g2.setup(cfg);
        const int seq2[8][2] = {
            {6, 7},  {7, 7},   // 黑堵(6,7)，白起(7,7)
            {0, 0},  {8, 7},   //
            {0, 5},  {9, 7},   //
            {0, 10}, {10, 7},  // 白形成 7-8-9-10 冲四，唯一成五点是 (11,7)
        };
        for (const auto& s : seq2)
        {
            chess::Move m{};
            m.to = chess::Coord{s[0], s[1]};
            expect(g2.apply(m), "setup2 move should be legal");
        }
        expect(g2.sideToMove() == chess::Side::First, "black to move in block test");

        const auto out2 = ai.search(g2, 3, 800, cancel, nullptr);
        expect(out2.hasMove, "AI should return a blocking move");
        expect(out2.move.to == chess::Coord{11, 7},
               "AI must play the single point (11,7) that blocks the opponent's four");
    }

    // ---- 6) AI 对 AI 能终局（防止出现"两边互相不落子/死循环"）----
    {
        chess::GameConfig cfg;
        cfg.cols = 9;
        cfg.rows = 9;
        cfg.variant = 0;

        chess::GomokuGame g;
        g.setup(cfg);

        chess::GomokuAi ai;
        std::atomic<bool> cancel{false};

        int moves = 0;
        while (!g.isOver() && moves < 9 * 9)
        {
            const auto out = ai.search(g, 2, 120, cancel, nullptr);
            if (!out.hasMove || !g.isLegal(out.move))
            {
                std::printf("[gomoku] FAIL: AI produced an illegal/absent move at move %d "
                            "(to = %d,%d)\n",
                            moves, out.move.to.x, out.move.to.y);
                ++failures;
                break;
            }
            g.apply(out.move);
            ++moves;
        }

        std::printf("[gomoku] ai-vs-ai 9x9: %d moves, status = %d (%s)\n", moves,
                    static_cast<int>(g.status()),
                    g.status() == chess::GameStatus::Draw      ? "draw"
                    : g.status() == chess::GameStatus::FirstWin ? "black wins"
                    : g.status() == chess::GameStatus::SecondWin ? "white wins"
                                                                 : "still playing");
        expect(g.isOver(), "ai-vs-ai game should reach a terminal state");
    }

    std::printf("[gomoku] %s (%d failures)\n", failures == 0 ? "ALL OK" : "HAS FAILURES", failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  象棋 perft
// ---------------------------------------------------------------------------

namespace {

// 统计深度 depth 的合法着法总数。
// 用 clone + apply 而不是自己写走子逻辑，这样测的才是游戏真正用的那套规则。
uint64_t perftCount(const chess::XiangqiGame& g, int depth)
{
    if (depth <= 0)
    {
        return 1;
    }

    const auto moves = g.legalMoves();
    if (depth == 1)
    {
        return moves.size();
    }

    uint64_t total = 0;
    for (const chess::Move& m : moves)
    {
        chess::XiangqiGame child = g;
        if (!child.apply(m))
        {
            continue;
        }
        total += perftCount(child, depth - 1);
    }
    return total;
}

} // namespace

int xiangqiPerft()
{
    int failures = 0;

    struct Case
    {
        int      depth;
        uint64_t expect;
    };
    // 参考值来自中国象棋标准初始局面的公开 perft 结果
    const Case cases[] = {
        {1, 44},
        {2, 1920},
        {3, 79666},
    };

    chess::GameConfig cfg;
    cfg.cols = 9;
    cfg.rows = 10;

    for (const Case& c : cases)
    {
        chess::XiangqiGame g;
        g.setup(cfg);

        const uint64_t got = perftCount(g, c.depth);
        const bool     ok  = (got == c.expect);
        if (!ok)
        {
            ++failures;
        }
        std::printf("[xq] perft(%d) = %llu   expect %llu   %s\n", c.depth,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(c.expect), ok ? "ok" : "MISMATCH");
    }

    // ---- 几个规则的定向检查（perft 过了也值得单独盯住，便于定位）----
    {
        // 1) 初始局面不应有人被将军，也不应该照面
        chess::XiangqiGame g;
        g.setup(cfg);
        if (g.inCheck(chess::xq::kRed) || g.inCheck(chess::xq::kBlack))
        {
            std::printf("[xq] FAIL: initial position reports check\n");
            ++failures;
        }
        if (g.kingsFacing())
        {
            std::printf("[xq] FAIL: initial position reports kings facing\n");
            ++failures;
        }

        // 2) 马腿：堵住马腿后，**这匹马本身**的着法应变少。
        //    注意不能断言"总着法数减少 2" —— 去堵马腿的那一步棋本身也会改变
        //    该棋子的机动性，总着法数的净变化并不等于马的变化量。
        //    所以这里只统计起点是那匹马的着法。
        {
            chess::XiangqiGame h;
            h.setup(cfg);

            const chess::Coord horse{1, 9};   // 红马
            auto countHorse = [&](const chess::XiangqiGame& g) {
                int n = 0;
                for (const chess::Move& m : g.legalMoves())
                {
                    if (m.from == horse)
                    {
                        ++n;
                    }
                }
                return n;
            };

            const int before = countHorse(h);

            chess::Move block{};
            block.from = chess::Coord{1, 7};   // 红炮
            block.to   = chess::Coord{1, 8};   // 正好堵住红马的马腿
            const bool moved = h.apply(block);
            const int  after = countHorse(h);

            std::printf("[xq] horse-leg: horse moves %d -> %d after blocking (1,8)   %s\n",
                        before, after, (moved && before == 2 && after == 0) ? "ok" : "MISMATCH");
            if (!moved || before != 2 || after != 0)
            {
                ++failures;
                std::printf("[xq] FAIL: red horse at (1,9) should have exactly 2 moves, "
                            "and 0 once its leg (1,8) is blocked\n");
            }
        }

        // 3) 飞将：把中路清空后，"移开最后一个挡子造成将帅照面"的那一步必须非法。
        //    走法序列：红兵(4,6)->(4,5)，黑卒(4,3)->(4,4)，红兵(4,5)吃(4,4)，
        //    此后 x=4 这条线上只剩红兵在 (4,4) 作为唯一遮挡。
        {
            chess::XiangqiGame f;
            f.setup(cfg);

            auto play = [&](int fx, int fy, int tx, int ty) {
                chess::Move m{};
                m.from = chess::Coord{fx, fy};
                m.to   = chess::Coord{tx, ty};
                return f.apply(m);
            };

            const bool m1 = play(4, 6, 4, 5);
            const bool m2 = play(4, 3, 4, 4);
            const bool m3 = play(4, 5, 4, 4);

            // 此时红兵在 (4,4)，是将帅之间唯一的子
            const bool onlyBlocker = (f.at(4, 4) != 0);

            // 红兵过河后可以横走；但这一步会让将帅照面 -> 必须判非法
            chess::Move expose{};
            expose.from = chess::Coord{4, 4};
            expose.to   = chess::Coord{3, 4};
            const bool exposeLegal = f.isLegal(expose);

            std::printf("[xq] flying-general: moves=%d%d%d blockerAt(4,4)=%d "
                        "sidewaysMoveLegal=%d   %s\n",
                        m1 ? 1 : 0, m2 ? 1 : 0, m3 ? 1 : 0, onlyBlocker ? 1 : 0,
                        exposeLegal ? 1 : 0,
                        (m1 && m2 && m3 && onlyBlocker && !exposeLegal) ? "ok" : "MISMATCH");

            if (!(m1 && m2 && m3) || !onlyBlocker || exposeLegal)
            {
                ++failures;
                std::printf("[xq] FAIL: moving the last blocker away must be illegal "
                            "(kings would face each other)\n");
            }

            // 顺带：当前局面不应报告照面
            if (f.kingsFacing())
            {
                ++failures;
                std::printf("[xq] FAIL: position with a blocker should not report kings facing\n");
            }
        }
    }

    std::printf("[xq] %s (%d failures)\n", failures == 0 ? "ALL OK" : "HAS FAILURES",
                failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  棋子 -> 图集精灵 映射核对
// ---------------------------------------------------------------------------

int dumpBoard()
{
    using namespace chess::xq;

    chess::Atlas& atlas = chess::App::inst().atlas();

    const PieceType types[7] = {
        PieceType::King,  PieceType::Advisor, PieceType::Elephant, PieceType::Horse,
        PieceType::Chariot, PieceType::Cannon, PieceType::Pawn,
    };
    const char* typeNames[7] = {"King",   "Advisor", "Elephant", "Horse",
                               "Chariot", "Cannon",  "Pawn"};

    int missing = 0;

    std::printf("[xq-map] %-4s %-9s %-12s %s\n", "side", "piece", "sprite", "inAtlas");
    for (int s = 0; s < 2; ++s)
    {
        const uint8_t side = (s == 0) ? kRed : kBlack;
        const char*   sideName = (s == 0) ? "red" : "black";

        for (int t = 0; t < 7; ++t)
        {
            const uint8_t piece  = makePiece(side, types[t]);
            const char*   sprite = chess::XiangqiGame::spriteNameForPiece(piece);

            const bool found = sprite && atlas.has(sprite);
            if (!found)
            {
                ++missing;
            }

            std::printf("[xq-map] %-4s %-9s %-12s %s%s\n", sideName, typeNames[t],
                        sprite ? sprite : "(none)", found ? "yes" : "no",
                        found ? "" : "   <- text fallback");
        }
    }

    std::printf("[xq-map] %d of 14 pieces have no usable atlas sprite\n", missing);

    // ---- 初始局面：逐打印"每格用哪个精灵（或 TEXT）" ----
    chess::GameConfig cfg;
    cfg.cols = 9;
    cfg.rows = 10;
    chess::XiangqiGame g;
    g.setup(cfg);

    std::printf("[xq-map] initial position (each cell shows the sprite name, "
                "'.' = empty)\n");

    // 列标
    std::printf("[xq-map]      ");
    for (int x = 0; x < chess::xq::kCols; ++x)
    {
        std::printf("%-10d", x);
    }
    std::printf("\n");

    for (int y = 0; y < chess::xq::kRows; ++y)
    {
        std::printf("[xq-map] y=%-2d ", y);
        for (int x = 0; x < chess::xq::kCols; ++x)
        {
            const uint8_t p = g.at(x, y);
            if (chess::xq::isEmpty(p))
            {
                std::printf("%-10s", ".");
                continue;
            }
            const char* sprite = chess::XiangqiGame::spriteNameForPiece(p);
            std::printf("%-10s", (sprite && atlas.has(sprite)) ? sprite : "TEXT");
        }
        std::printf("\n");
    }

    // 图集里有哪些名字，方便对照是否用错
    std::printf("[xq-map] atlas sprites: ");
    for (const std::string& n : atlas.names())
    {
        std::printf("%s ", n.c_str());
    }
    std::printf("\n");

    return 0;
}

// ---------------------------------------------------------------------------
//  围棋规则自检
// ---------------------------------------------------------------------------

namespace {

using chess::Coord;
using chess::Move;
using chess::Side;

Move pt(int x, int y)
{
    Move m{};
    m.to = Coord{x, y};
    return m;
}

Move passMove()
{
    return Move{};
}

} // namespace

int goRulesTest()
{
    int failures = 0;
    auto expect = [&](bool cond, const char* what) {
        if (!cond)
        {
            ++failures;
            std::printf("[go] FAIL: %s\n", what);
        }
        return cond;
    };

    chess::GameConfig cfg;
    cfg.cols = 9;
    cfg.rows = 9;
    cfg.handicap = 0;

    // ---- 1) 提子：黑把白一子四面包住 ----
    {
        chess::GoGame g;
        g.setup(cfg);
        expect(g.sideToMove() == Side::First, "black moves first with no handicap");

        // 白 (4,4)，黑围 (3,4) (5,4) (4,3)，最后 (4,5)
        g.apply(pt(3, 4));   // 黑
        g.apply(pt(4, 4));   // 白
        g.apply(pt(5, 4));   // 黑
        g.apply(pt(0, 0));   // 白（无关）
        g.apply(pt(4, 3));   // 黑
        g.apply(pt(0, 1));   // 白
        expect(g.at(4, 4) == 2, "white stone should still be on board before last liberty");

        const bool ok = g.apply(pt(4, 5));   // 黑：提
        expect(ok, "capturing move should be legal");
        std::printf("[go] capture: (4,4) after capture = %d (expect 0)\n", g.at(4, 4));
        expect(g.at(4, 4) == 0, "captured white stone should be removed");
    }

    // ---- 2) 提整块 + 悔棋复原每一枚 ----
    //  白两子在 (4,4) (5,4)，黑棋需要把 6 个邻点全占住：
    //    (3,4) (6,4) (4,3) (5,3) (5,5) (4,5)
    //  前 5 个占完后白块只剩 (4,5) 一口气，第 6 手才提掉。
    {
        chess::GoGame g;
        g.setup(cfg);

        g.apply(pt(3, 4));   // B
        g.apply(pt(4, 4));   // W
        g.apply(pt(6, 4));   // B
        g.apply(pt(5, 4));   // W
        g.apply(pt(4, 3));   // B
        g.apply(pt(0, 0));   // W（无关）
        g.apply(pt(5, 3));   // B
        g.apply(pt(0, 1));   // W
        g.apply(pt(5, 5));   // B
        g.apply(pt(0, 2));   // W
        expect(g.at(4, 4) == 2 && g.at(5, 4) == 2,
               "both white stones present before the final capture");

        const bool ok = g.apply(pt(4, 5));   // B：提掉整块
        expect(ok, "group-capturing move should be legal");
        std::printf("[go] group capture: (4,4)=%d (5,4)=%d (expect 0 0)\n",
                    g.at(4, 4), g.at(5, 4));
        expect(g.at(4, 4) == 0 && g.at(5, 4) == 0,
               "BOTH stones of the group must be removed");

        // 悔棋：两枚都要回来
        g.undo();
        std::printf("[go] after undo:      (4,4)=%d (5,4)=%d (expect 2 2)\n",
                    g.at(4, 4), g.at(5, 4));
        expect(g.at(4, 4) == 2 && g.at(5, 4) == 2,
               "undo must restore EVERY captured stone, not just the group seed");
        expect(g.at(4, 5) == 0, "undo must remove the stone that was played");
    }

    // ---- 3) 禁自杀 ----
    {
        chess::GoGame g;
        g.setup(cfg);

        // 黑围住 (1,1)（角上），白若填进去就是自杀
        g.apply(pt(0, 1));   // 黑
        g.apply(pt(5, 5));   // 白
        g.apply(pt(1, 0));   // 黑
        g.apply(pt(5, 6));   // 白
        g.apply(pt(2, 1));   // 黑（此时 (1,1) 只剩 (1,1) 自己这一口气点）
        g.apply(pt(6, 5));   // 白
        g.apply(pt(1, 2));   // 黑 —— (1,1) 被完全包围

        const Move suicide = pt(1, 1);
        std::printf("[go] suicide: legal=%d (expect 0)\n", g.isLegal(suicide) ? 1 : 0);
        expect(!g.isLegal(suicide), "filling own last liberty must be illegal (suicide)");
    }

    // ---- 4) 简单劫（打劫）----
    //  劫形的构造条件（两句话就能验证）：
    //    * 白 A 子被黑三面包围，只剩 E 一个气点
    //    * 黑在 E 落子提掉 A 后，黑 E 自身的三个非-A 邻点都是白子
    //      -> 黑 E 只剩 A 这一口气，白才有"提回"的资格（于是禁着）
    //  取 A=(2,2)、E=(2,1)，于是白需占 (2,0)(1,1)(3,1)，黑需占 (1,2)(3,2)(2,3)。
    {
        chess::GoGame g;
        g.setup(cfg);

        g.apply(pt(1, 2));   // B
        g.apply(pt(2, 2));   // W  <- A
        g.apply(pt(3, 2));   // B
        g.apply(pt(2, 0));   // W
        g.apply(pt(2, 3));   // B
        g.apply(pt(1, 1));   // W
        g.apply(pt(7, 7));   // B（等待一手）
        g.apply(pt(3, 1));   // W
        // 此时白 A(2,2) 只剩 (2,1) 一气，(2,1) 的三个非 A 邻点都是白子
        expect(g.at(2, 2) == 2 && g.at(2, 0) == 2 && g.at(1, 1) == 2 && g.at(3, 1) == 2,
               "ko setup: white stones in place");

        const bool cap = g.apply(pt(2, 1));   // B：提掉 A，自身只剩 A 这一气
        expect(cap, "ko capture should be legal");
        expect(g.at(2, 2) == 0, "ko: white stone should be captured");
        expect(g.isKoPoint(Coord{2, 2}), "ko point should be set at the captured spot");

        // 白立刻提回 -> 非法
        const Move retake = pt(2, 2);
        std::printf("[go] ko: immediate retake legal=%d (expect 0)\n",
                    g.isLegal(retake) ? 1 : 0);
        expect(!g.isLegal(retake), "immediate ko retake must be illegal");

        // 白先走别处，黑也走别处，之后白就可以提回
        g.apply(pt(8, 8));   // W 找劫材
        std::printf("[go] ko: retake after a ko threat legal=%d (expect 1)\n",
                    g.isLegal(retake) ? 1 : 0);
        expect(g.isLegal(retake), "ko retake becomes legal after an intervening move");
    }

    // ---- 5) 停手终局 ----
    {
        chess::GoGame g;
        g.setup(cfg);
        g.apply(pt(4, 4));   // 黑
        g.apply(passMove());   // 白停
        expect(!g.isOver(), "one pass should not end the game");
        g.apply(passMove());   // 黑停
        std::printf("[go] two passes -> status=%d  %s\n", static_cast<int>(g.status()),
                    g.isOver() ? "over" : "still playing");
        expect(g.isOver(), "two consecutive passes must end the game");
    }

    // ---- 6) 数子：单色围空归该色 ----
    {
        chess::GoGame g;
        g.setup(cfg);

        // 黑在角上围一个 1x1：(0,0) 空，(0,1) 与 (1,0) 黑，(1,1) 黑
        g.apply(pt(0, 1));   // B
        g.apply(pt(8, 8));   // W
        g.apply(pt(1, 0));   // B
        g.apply(pt(8, 7));   // W
        g.apply(pt(1, 1));   // B
        g.apply(passMove());   // W
        g.apply(passMove());   // B -> 终局

        const auto s = g.score();
        // 黑：3 子 + (0,0) 这 1 点 = 4；白：2 子
        std::printf("[go] score: black=%.1f white=%.1f(+komi %.1f)  expect black>=4 white>=2\n",
                    static_cast<double>(s.black), static_cast<double>(s.white), s.komi);
        expect(s.black >= 4, "black must include the enclosed 1x1 territory");
        expect(s.white >= 2, "white stones must be counted");
    }

    // ---- 7) AI 能正常出着 + MCTS 的模拟没有退化成"走几手就截断" ----
    //  这里刻意断言**平均每局模拟手数**，而不是只断言"AI 能出着"：
    //  如果 playout 内部的计时基准写错（例如误用进程累计时间比相对预算），
    //  每次模拟都会在头十几手被截断，MCTS 实际退化成随机。
    //  这种状态下 AI 依然会返回合法着法，只测"能走棋"是抓不到的。
    {
        chess::GoGame g;
        g.setup(cfg);

        chess::GoAi       ai;
        std::atomic<bool> cancel{false};

        int    moves      = 0;
        int    avgSum     = 0;
        int    avgCount   = 0;
        int    playouts   = 0;

        while (!g.isOver() && moves < 200)
        {
            const auto out = ai.search(g, 3, 250, cancel, nullptr);
            if (!out.hasMove || !g.isLegal(out.move))
            {
                std::printf("[go] FAIL: AI produced illegal/absent move on move %d "
                            "(to=%d,%d)\n",
                            moves, out.move.to.x, out.move.to.y);
                ++failures;
                break;
            }
            if (out.hint.mctsAvgPlayoutLen > 0)
            {
                avgSum += out.hint.mctsAvgPlayoutLen;
                ++avgCount;
            }
            playouts += out.hint.mctsPlayouts;

            g.apply(out.move);
            ++moves;
        }

        const int avgLen = avgCount > 0 ? avgSum / avgCount : 0;
        std::printf("[go] ai-vs-ai 9x9: %d moves, status=%d, mcts playouts=%d, "
                    "avg playout len=%d\n",
                    moves, static_cast<int>(g.status()), playouts, avgLen);

        expect(moves > 5, "AI self-play should make progress");

        // 这条断言的目的很具体：抓住"模拟一开手就被截断"的退化。
        //   * 计时基准写错（用进程累计时间去比相对预算）时，每次模拟都在
        //     第一个检查点就返回，平均值会掉到 0 —— AI 照样返回合法着法，
        //     只测"能走棋"完全抓不到。
        //   * 健康值时该值在 25~40 之间：MCTS 树变深后模拟从深层节点开始，
        //     盘面已有棋子，剩余手数自然不多。所以阈值取 15 而不是更大 ——
        //     这不是在断言"一局棋有多长"，只是在断言"模拟确实跑起来了"。
        expect(avgLen >= 15,
               "MCTS playouts abort immediately (avg length ~0) -> "
               "check the timer used inside playout");
    }

    std::printf("[go] %s (%d failures)\n", failures == 0 ? "ALL OK" : "HAS FAILURES", failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  AI 时间预算回归测试
// ---------------------------------------------------------------------------

int aiBudgetTest()
{
    int failures = 0;

    constexpr int kRuns   = 4;
    constexpr int kBudget = 400;   // 每次搜索的时限（毫秒）

    // 用"搜索节点数"作为"是否真的在用时间预算"的代理指标：
    // 一旦计时器失效立刻中止，节点数会比正常值小几个数量级。
    auto report = [&](const char* label, const int* nodes, const int* ms) {
        std::printf("[ai-budget] %-9s nodes:", label);
        for (int i = 0; i < kRuns; ++i)
        {
            std::printf(" %8d", nodes[i]);
        }
        std::printf("   ms:");
        for (int i = 0; i < kRuns; ++i)
        {
            std::printf(" %5d", ms[i]);
        }
        std::printf("\n");

        // 第 2 次起的节点数不应塌到第一次的 1/50 以下
        const int first = std::max(1, nodes[0]);
        for (int i = 1; i < kRuns; ++i)
        {
            if (nodes[i] * 50 < first)
            {
                std::printf("[ai-budget] FAIL: %s run #%d used %d nodes vs run #0's %d "
                            "-> search aborted early (stale timer?)\n",
                            label, i, nodes[i], first);
                ++failures;
            }
        }
    };

    // ---- 五子棋 ----
    {
        chess::GameConfig cfg;
        cfg.cols = 15;
        cfg.rows = 15;
        cfg.variant = 0;

        chess::GomokuGame g;
        g.setup(cfg);
        // 摆几手，避免开局候选点太少
        const int seq[6][2] = {{7, 7}, {8, 7}, {7, 8}, {8, 8}, {6, 6}, {9, 9}};
        for (const auto& s : seq)
        {
            chess::Move m{};
            m.to = chess::Coord{s[0], s[1]};
            g.apply(m);
        }

        chess::GomokuAi      ai;
        std::atomic<bool>    cancel{false};
        int                  nodes[kRuns]{};
        int                  ms[kRuns]{};

        for (int i = 0; i < kRuns; ++i)
        {
            const auto out = ai.search(g, 4, kBudget, cancel, nullptr);
            nodes[i] = static_cast<int>(out.hint.nodes);
            ms[i]    = out.hint.thinkingMs;
        }
        report("gomoku", nodes, ms);
    }

    // ---- 象棋 ----
    {
        chess::GameConfig cfg;
        cfg.cols = 9;
        cfg.rows = 10;

        chess::XiangqiGame g;
        g.setup(cfg);

        chess::XiangqiAi   ai;
        std::atomic<bool>  cancel{false};
        int                nodes[kRuns]{};
        int                ms[kRuns]{};

        for (int i = 0; i < kRuns; ++i)
        {
            const auto out = ai.search(g, 4, kBudget, cancel, nullptr);
            nodes[i] = static_cast<int>(out.hint.nodes);
            ms[i]    = out.hint.thinkingMs;
        }
        report("xiangqi", nodes, ms);
    }

    std::printf("[ai-budget] %s (%d failures)\n",
                failures == 0 ? "ALL OK" : "HAS FAILURES", failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  象棋棋子叠层渲染自检
// ---------------------------------------------------------------------------

int xiangqiPieceRenderTest()
{
    int failures = 0;

    chess::Atlas& atlas = chess::App::inst().atlas();

    // 与 GameScene 一致的棋盘区域：内容区去掉右侧 HUD
    const chess::Rect boardArea{0, chess::theme::kTopBarH,
                                chess::kWindowWidth - chess::theme::kHudW,
                                chess::kWindowHeight - chess::theme::kTopBarH};

    chess::GameConfig cfg;
    cfg.cols = 9;
    cfg.rows = 10;

    chess::XiangqiGame g;
    g.setup(cfg);

    chess::BoardView bv;
    bv.layout(g.boardSpec(), boardArea);

    PIMAGE img = newimage(chess::kWindowWidth, chess::kWindowHeight);
    if (!img)
    {
        std::printf("[xq-piece] newimage failed\n");
        return 1;
    }

    // 渲染棋盘 + 棋子（与对局场景同一套调用顺序）
    setbkcolor(chess::theme::kBg, img);
    cleardevice(img);
    bv.drawBoardBase(img);
    bv.drawGrid(img);

    chess::DrawContext ctx;
    ctx.atlas     = &atlas;
    ctx.hintLevel = 0;
    ctx.showHint  = false;

    g.drawDecorations(bv, ctx, img);
    for (int y = 0; y < bv.rows(); ++y)
    {
        for (int x = 0; x < bv.cols(); ++x)
        {
            g.drawCell(bv, chess::Coord{x, y}, ctx, img);
        }
    }

    // ---- 环形采样 ----
    // 棋盘底色与棋盘线不算数；底盘/字面/描边都算"有东西"
    const color_t bg   = chess::theme::kBoardBg & 0x00FFFFFFu;
    const color_t line = chess::theme::kBoardLine & 0x00FFFFFFu;

    auto ringCoverage = [&](chess::Coord c, int samples) {
        const int cx = bv.px(c);
        const int cy = bv.py(c);
        const int r  = std::max(4, static_cast<int>(bv.cell() * 0.30));

        int onPiece = 0;
        for (int i = 0; i < samples; ++i)
        {
            const double a  = 2.0 * 3.14159265358979 * i / samples;
            const int    px = static_cast<int>(std::lround(cx + r * std::cos(a)));
            const int    py = static_cast<int>(std::lround(cy + r * std::sin(a)));

            if (px < 0 || py < 0 || px >= chess::kWindowWidth || py >= chess::kWindowHeight)
            {
                continue;
            }
            const color_t v = getpixel_f(px, py, img) & 0x00FFFFFFu;
            if (v != bg && v != line)
            {
                ++onPiece;
            }
        }
        return onPiece;
    };

    constexpr int kSamples = 16;

    int occupiedChecked = 0;
    int occupiedBad     = 0;

    for (int y = 0; y < bv.rows(); ++y)
    {
        for (int x = 0; x < bv.cols(); ++x)
        {
            const chess::Coord c{x, y};
            if (chess::xq::isEmpty(g.at(x, y)))
            {
                continue;
            }
            ++occupiedChecked;

            const int hits = ringCoverage(c, kSamples);
            // 阈值留出余量：字面笔画与棋盘标记可能穿过采样圈，
            // 但只要有底盘，这一圈大部分都应被覆盖。
            if (hits < kSamples * 3 / 4)
            {
                ++occupiedBad;
                std::printf("[xq-piece] piece at (%d,%d) sprite='%s' ring=%d/%d "
                            "-> base layer MISSING?\n",
                            x, y,
                            chess::XiangqiGame::spriteNameForPiece(g.at(x, y))
                                ? chess::XiangqiGame::spriteNameForPiece(g.at(x, y))
                                : "(text)",
                            hits, kSamples);
            }
        }
    }

    // ---- 附加检查：14 颗棋子必须**每一颗**都有可用精灵 ----
    //  红炮曾经因为没有独立素材而退化去画文字，导致同一个"炮"字在棋盘上
    //  呈现两种完全不同的风格（图集字面 vs 自绘文本）。红黑两方的炮字形
    //  本来就相同，共用同一个 pao 精灵才是对的。
    //  这条断言防止将来又有人把某个棋子改回 nullptr。
    int  missingSprite = 0;
    // 记录时用 ASCII（红/黑 + 棋子类型名）而不是中文棋子名：
    // 源码是按 GBK 生成窄字符串的，而 VS Code 终端是 UTF-8，直接打中文会乱码。
    const char* missingList[16]{};
    int         missingCount = 0;
    char        missingBuf[16][24]{};

    const char* kTypeNames[8] = {"none",    "king",  "advisor", "elephant",
                                 "horse",   "chariot", "cannon", "pawn"};

    for (int s = 0; s < 2; ++s)
    {
        const uint8_t side = (s == 0) ? chess::xq::kRed : chess::xq::kBlack;
        for (int t = 1; t <= 7; ++t)
        {
            const uint8_t piece = chess::xq::makePiece(
                side, static_cast<chess::xq::PieceType>(t));
            const char* sprite = chess::XiangqiGame::spriteNameForPiece(piece);
            if (!sprite || !atlas.has(sprite))
            {
                ++missingSprite;
                if (missingCount < 16)
                {
                    std::snprintf(missingBuf[missingCount], sizeof(missingBuf[0]), "%s-%s",
                                  (s == 0) ? "red" : "black", kTypeNames[t]);
                    missingList[missingCount] = missingBuf[missingCount];
                    ++missingCount;
                }
            }
        }
    }

    if (missingSprite > 0)
    {
        std::printf("[xq-piece] pieces without a sprite: %d ->", missingSprite);
        for (int i = 0; i < missingCount; ++i)
        {
            std::printf(" %s", missingList[i]);
        }
        std::printf("\n");
    }

    // 对照组：空交叉点不该有多余像素
    int emptyBad = 0;
    const chess::Coord empties[] = {{1, 1}, {4, 1}, {4, 4}, {0, 5}, {8, 8}};
    for (const chess::Coord& c : empties)
    {
        if (ringCoverage(c, kSamples) > 0)
        {
            ++emptyBad;
        }
    }

    std::printf("[xq-piece] occupied=%d  baseMissing=%d  strayOnEmpty=%d/%d  "
                "noSprite=%d/14\n",
                occupiedChecked, occupiedBad, emptyBad,
                static_cast<int>(sizeof(empties) / sizeof(empties[0])), missingSprite);

    if (occupiedBad > 0)
    {
        ++failures;
        std::printf("[xq-piece] FAIL: %d pieces have no base layer\n", occupiedBad);
    }
    if (emptyBad > 0)
    {
        ++failures;
        std::printf("[xq-piece] FAIL: %d empty points have unexpected pixels\n", emptyBad);
    }
    if (missingSprite > 0)
    {
        ++failures;
        std::printf("[xq-piece] FAIL: %d pieces would fall back to drawn text "
                    "(prefer reusing an existing sprite when glyphs are identical)\n",
                    missingSprite);
    }

    delimage(img);
    std::printf("[xq-piece] %s (%d failures)\n",
                failures == 0 ? "ALL OK" : "HAS FAILURES", failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  中文编码链路自检
// ---------------------------------------------------------------------------

namespace {

// 把同一个字符串分别用"窄字符 API"和"宽字符 API"渲染到两张同尺寸的离屏图，
// 然后逐像素比对。
//
// 为什么这是有效的判据：宽字符重载直达 DrawTextW/TextOutW，**不经过代码页转换**，
// 所以它永远是"正确字形"的参照。若窄字符路径的编码链路出错（执行字符集没生效、
// 或活动代码页与字面量编码不一致），它解出的就是另一串字符 ——
// 字形完全不同，像素必然大面积不一致。
struct TextRenderProbe
{
    struct Diff
    {
        int inkA      = 0;   // 图 A 的墨迹像素数
        int inkB      = 0;   // 图 B 的墨迹像素数
        int differing = 0;   // 两图取值不同的像素数
    };

    static constexpr int kFontPx = 36;

    // 两张图必须用完全相同的字体与位置
    static void prepare(PIMAGE img)
    {
        setbkcolor(EGERGB(0xFF, 0xFF, 0xFF), img);
        cleardevice(img);
        setbkmode(TRANSPARENT, img);
        setfont(kFontPx, 0, chess::kUiFontFace(), img);
        setcolor(EGERGB(0x00, 0x00, 0x00), img);
    }

    static void renderNarrow(PIMAGE img, const char* s)
    {
        prepare(img);
        outtextxy(12, 30, s, img);
    }

    static void renderWide(PIMAGE img, const wchar_t* s)
    {
        prepare(img);
        outtextxy(12, 30, s, img);
    }

    static Diff compare(PIMAGE a, PIMAGE b)
    {
        Diff d;
        const int w = std::min(getwidth(a), getwidth(b));
        const int h = std::min(getheight(a), getheight(b));

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const color_t ca = getpixel_f(x, y, a) & 0x00FFFFFFu;
                const color_t cb = getpixel_f(x, y, b) & 0x00FFFFFFu;

                // 墨迹 = 非白底
                if (ca != 0x00FFFFFFu) { ++d.inkA; }
                if (cb != 0x00FFFFFFu) { ++d.inkB; }
                if (ca != cb) { ++d.differing; }
            }
        }
        return d;
    }
};

} // namespace

int encodingTest()
{
    int failures = 0;

    // 用来验证的样本：既有汉字也有 ASCII，便于区分"整体编码错"与"只有中文错"
    const char*    narrow = "棋类游戏合集 ABC 123";
    const wchar_t* wide   = L"棋类游戏合集 ABC 123";

    // ---- 1) 字面量的原始字节 ----
    //  期望（默认 --text_charset=gbk + MinGW）：汉字是 GBK 双字节，ASCII 不变
    //    "棋" = C6 E5   "类" = C0 E0   ...
    //  如果这里是 E4 xx xx（UTF-8 三字节形式），说明 -fexec-charset 没生效。
    std::printf("[enc] narrow literal bytes:");
    int nonAscii = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(narrow); *p; ++p)
    {
        std::printf(" %02X", *p);
        if (*p >= 0x80)
        {
            ++nonAscii;
        }
    }
    std::printf("\n");

    // 前 4 个非 ASCII 字节足以判定：GBK 是 2 字节/字，UTF-8 是 3 字节/字。
    // "棋类游戏合集" 共 6 个汉字 -> GBK 应有 12 字节，UTF-8 应有 18 字节。
    // 这里只统计到第一个 ASCII 空格之前。
    int cjkBytes = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(narrow); *p; ++p)
    {
        if (*p >= 0x80)
        {
            ++cjkBytes;
        }
        else if (*p == ' ')
        {
            break;
        }
    }

    const char* execCharset = "unknown";
    if (cjkBytes == 12)
    {
        execCharset = "GBK (2 bytes per CJK char)";
    }
    else if (cjkBytes == 18)
    {
        execCharset = "UTF-8 (3 bytes per CJK char)";
    }
    std::printf("[enc] exec charset looks like: %s  (%d bytes for 6 CJK chars; "
                "non-ascii total %d)\n",
                execCharset, cjkBytes, nonAscii);

    // ---- 2) 系统 ACP / EGE codepage / 往返转换 ----
    const UINT acp  = GetACP();
    const UINT egeCp = getcodepage();

    // EGE 窄字符重载内部做的事：MultiByteToWideChar(getcodepage(), ...)
    wchar_t converted[128]{};
    const int got = MultiByteToWideChar(static_cast<UINT>(CHESS_TEXT_CODEPAGE), 0, narrow, -1,
                                       converted, 128);

    std::printf("[enc] GetACP()=%u  EGE getcodepage()=%u  CHESS_TEXT_CODEPAGE=%u  "
                "MultiByteToWideChar -> %d chars\n",
                acp, egeCp, static_cast<UINT>(CHESS_TEXT_CODEPAGE), got);

    // 期望解出的宽字符：U+68CB U+7C7B U+6E38 U+620F U+5408 U+96C6
    const wchar_t expected[7] = {0x68CB, 0x7C7B, 0x6E38, 0x620F, 0x5408, 0x96C6, 0};
    const bool    roundTripOk = (got > 0) && (std::wcsncmp(converted, expected, 6) == 0);

    if (got > 0)
    {
        std::printf("[enc] first 6 converted code units:");
        for (int i = 0; i < 6 && converted[i]; ++i)
        {
            std::printf(" U+%04X", static_cast<unsigned>(converted[i]));
        }
        std::printf("   (expect U+68CB U+7C7B U+6E38 U+620F U+5408 U+96C6)\n");
    }

    if (!roundTripOk)
    {
        ++failures;
        std::printf("[enc] FAIL: code page conversion does not yield the expected "
                    "CJK code units\n");
        std::printf("[enc]   -> either the exec charset flag was dropped, or the "
                    "active code page is not the one used to encode the literal\n");
    }

    // ---- 3) 端到端像素比对：窄字符 vs 宽字符 ----
    //  宽字符路径不经过代码页转换，是"已知正确"的参照。
    constexpr int W = 520;
    constexpr int H = 120;

    PIMAGE imgNarrow = newimage(W, H);
    PIMAGE imgWide   = newimage(W, H);
    if (!imgNarrow || !imgWide)
    {
        std::printf("[enc] newimage failed\n");
        if (imgNarrow) { delimage(imgNarrow); }
        if (imgWide) { delimage(imgWide); }
        return 1;
    }

    TextRenderProbe probe;
    probe.renderNarrow(imgNarrow, narrow);
    probe.renderWide(imgWide, wide);

    const TextRenderProbe::Diff diff = TextRenderProbe::compare(imgNarrow, imgWide);

    std::printf("[enc] ink pixels: narrow=%d  wide=%d\n", diff.inkA, diff.inkB);
    std::printf("[enc] pixel diff : differing=%d  (of %d x %d)\n", diff.differing, W, H);

    // 字体画不出中文时两图都会是空白，会假通过 —— 所以先要求有足够墨迹
    const int kMinInk = 200;
    if (diff.inkA < kMinInk || diff.inkB < kMinInk)
    {
        ++failures;
        std::printf("[enc] FAIL: too little ink (%d / %d) -> the font probably cannot "
                    "render these glyphs, so this check would be meaningless\n",
                    diff.inkA, diff.inkB);
    }
    else if (diff.differing > 0)
    {
        ++failures;
        std::printf("[enc] FAIL: narrow and wide rendering differ -> the narrow-char "
                    "path is NOT producing the same glyphs (this is exactly what "
                    "garbled UI text looks like)\n");
    }
    else
    {
        std::printf("[enc] narrow and wide rendering are pixel-identical -> UI text "
                    "encoding chain is correct\n");
    }

    // 把两张图存下来，便于肉眼复核（存之前补 alpha，否则 GDI 图元会变黑）
    makeImageOpaque(imgNarrow);
    makeImageOpaque(imgWide);
    savepng(imgNarrow, "build/enc_narrow.png", false);
    savepng(imgWide, "build/enc_wide.png", false);

    delimage(imgNarrow);
    delimage(imgWide);

    std::printf("[enc] saved build/enc_narrow.png and build/enc_wide.png for eyeballing\n");

    // ---- 4) 跨编译单元检查（关键）----
    //
    //  上面的检查只用到了本文件（selftest.cc）里的字面量。
    //  这只覆盖了一个编译单元 —— 而**每个 .cc 文件是各自独立编译的**，
    //  执行字符集 flag 必须对每一个都生效。
    //
    //  踩过的坑：改动 add_cxflags 后 xmake 只重建了"源文件被改过"的那几个 TU，
    //  其余目标文件保持旧状态（仍是 UTF-8 字面量）。
    //  此时界面里**只有部分文本乱码**，而只测本文件的检查会全部通过。
    //
    //  所以这里从**别的编译单元**取真实字符串（各游戏的 desc().name、
    //  sideName、等级名等），再与本文件里写的同一个文本（宽字符形式）做像素比对。
    //  哪一个 TU 没被正确编译，就会在这里暴露成具体的一行。
    {
        struct TuSample
        {
            const char*    label;
            const char*    fromOtherTu;
            const wchar_t* expectedWidth;
        };

        // 注意：这些字符串分别定义在 gomoku.cc / xiangqi.cc / go.cc /
        // flip_puzzle.cc / registry.cc 里。
        // 千万不要在这里直接写字面量当作"来自别的 TU"—— 那样测的仍是本文件。
        const chess::GomokuGame   gomokuGame;
        const chess::XiangqiGame  xiangqiGame;
        const chess::GoGame       goGame;
        const chess::GameDesc gomokuDesc  = gomokuGame.desc();
        const chess::GameDesc xiangqiDesc = xiangqiGame.desc();
        const chess::GameDesc goDesc      = goGame.desc();
        const chess::GameDesc flipDesc    = chess::FlipPuzzleGame{}.desc();
        const std::vector<chess::LevelDesc>& levels = chess::defaultLevels();

        const TuSample tuSamples[] = {
            {"gomoku.cc    name", gomokuDesc.name, L"五子棋"},
            {"xiangqi.cc   name", xiangqiDesc.name, L"象棋"},
            {"go.cc        name", goDesc.name, L"围棋"},
            {"flip_puzzle  name", flipDesc.name, L"翻转棋"},
            {"registry.cc  level", levels.empty() ? "" : levels[0].name, L"入门"},
            {"gomoku.cc    side", gomokuGame.sideName(chess::Side::First), L"黑棋"},
            {"xiangqi.cc   side", xiangqiGame.sideName(chess::Side::First), L"红方"},
            {"go.cc         side", goGame.sideName(chess::Side::Second), L"白棋"},
            {"gomoku.cc   blurb", gomokuDesc.blurb,
             L"先连成五子者胜；可选黑棋禁手（长连 / 双四 / 双活三）"},
            // UI 层也要覆盖 —— 菜单、图集查看器的标题都是用户直接看到的文本
            {"menu_scene.cc name", chess::MenuScene{}.name(), L"主菜单"},
            {"atlas_scene  name", chess::AtlasScene{}.name(), L"图集查看器"},
        };

        constexpr int TW = 320;
        constexpr int TH = 96;

        PIMAGE a = newimage(TW, TH);
        PIMAGE b = newimage(TW, TH);

        if (!a || !b)
        {
            ++failures;
            std::printf("[enc] FAIL: newimage failed for the cross-TU check\n");
        }
        else
        {
            int tuBad = 0;
            for (const TuSample& s : tuSamples)
            {
                if (!s.fromOtherTu || !*s.fromOtherTu)
                {
                    ++failures;
                    ++tuBad;
                    std::printf("[enc] FAIL: %s -> empty string from other TU\n", s.label);
                    continue;
                }

                TextRenderProbe::renderNarrow(a, s.fromOtherTu);
                TextRenderProbe::renderWide(b, s.expectedWidth);

                const TextRenderProbe::Diff d = TextRenderProbe::compare(a, b);
                const bool ok = (d.inkA >= 60) && (d.inkB >= 60) && (d.differing == 0);
                if (!ok)
                {
                    ++tuBad;
                    ++failures;
                }

                std::printf("[enc] %-18s ink=%4d/%4d diff=%5d  %s\n", s.label, d.inkA,
                            d.inkB, d.differing, ok ? "ok" : "GARBLED");
            }

            if (tuBad > 0)
            {
                std::printf("[enc] FAIL: %d translation unit(s) carry wrongly encoded "
                            "literals.\n", tuBad);
                std::printf("[enc]   If the source files are correct, this usually means the "
                            "objects are STALE:\n");
                std::printf("[enc]   changing add_cxflags does not always invalidate existing "
                            "objects -> do a clean rebuild\n");
                std::printf("[enc]       xmake f -c ... && xmake -r\n");
            }
            else
            {
                std::printf("[enc] all sampled translation units render identically -> "
                            "every TU was compiled with the right exec charset\n");
            }

            delimage(a);
            delimage(b);
        }
    }

    // ---- 5) 产物级扫描：直接查可执行文件里有没有 UTF-8 编码的中文 【覆盖面最广】----
    //
    //  前面的检查都依赖"我能通过 API 取到那个 TU 的字符串"。
    //  有些编译单元（例如 app.cc 的窗口标题、game_scene.cc 的按钮文案）
    //  的字符串藏在函数内部，外部拿不到。
    //
    //  但这件事可以在**产物**上直接查：正确构建时所有中文都是 GBK 双字节，
    //  可执行文件里**不应该出现连续的 UTF-8 中文三联字节**。
    //  UTF-8 中文 = [E4..E9][80..BF][80..BF]，连续 4 个汉字就是 12 字节的强特征；
    //  而 GBK 中文的长相是 [81..FE][40..FE]，极少能凑出这种结构。
    //  所以"在二进制里找到 >=4 个连续 UTF-8 汉字"就等价于"某个 TU 用了 UTF-8"。
    //
    //  对照自检：同一个扫描器也跑一遍**源码文件**（源码是 UTF-8），
    //  必须在那里找到若干 run —— 否则说明扫描器本身失效，结果不可信。
    {
        auto countUtf8Runs = [](const char* path, int* outRuns, int* outLongest,
                                size_t* outFirstOffset) {
            *outRuns = 0;
            *outLongest = 0;
            *outFirstOffset = 0;

            std::FILE* fp = std::fopen(path, "rb");
            if (!fp)
            {
                return false;
            }
            std::fseek(fp, 0, SEEK_END);
            const long sz = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);

            std::vector<unsigned char> buf(static_cast<size_t>(sz > 0 ? sz : 0));
            const size_t got = std::fread(buf.data(), 1, buf.size(), fp);
            std::fclose(fp);

            constexpr int kMinRun = 4;
            size_t        i = 0;
            while (i + 2 < got)
            {
                const unsigned char b0 = buf[i];
                if (b0 >= 0xE4 && b0 <= 0xE9 && buf[i + 1] >= 0x80 && buf[i + 1] <= 0xBF &&
                    buf[i + 2] >= 0x80 && buf[i + 2] <= 0xBF)
                {
                    int    n = 0;
                    size_t j = i;
                    while (j + 2 < got && buf[j] >= 0xE4 && buf[j] <= 0xE9 &&
                           buf[j + 1] >= 0x80 && buf[j + 1] <= 0xBF &&
                           buf[j + 2] >= 0x80 && buf[j + 2] <= 0xBF)
                    {
                        ++n;
                        j += 3;
                    }
                    if (n >= kMinRun)
                    {
                        ++(*outRuns);
                        if (n > *outLongest)
                        {
                            *outLongest     = n;
                            *outFirstOffset = i;
                        }
                    }
                    i = j;
                    continue;
                }
                ++i;
            }
            return true;
        };

        // 对照组：源码是 UTF-8，必须被检出
        int    ctrlRuns    = 0;
        int    ctrlLongest = 0;
        size_t ctrlOff     = 0;
        const bool ctrlRead =
            countUtf8Runs("src/app/selftest.cc", &ctrlRuns, &ctrlLongest, &ctrlOff);

        std::printf("[enc] control scan of src/app/selftest.cc (UTF-8 source): "
                    "%d run(s), longest %d chars\n", ctrlRuns, ctrlLongest);

        if (ctrlRead && ctrlRuns == 0)
        {
            ++failures;
            std::printf("[enc] FAIL: the scanner found nothing in a known UTF-8 file -> "
                        "the scanner itself is broken, so its verdict is meaningless\n");
        }

        // 被测对象：可执行文件应当是纯 GBK
        int    exeRuns    = 0;
        int    exeLongest = 0;
        size_t exeOff     = 0;
        const bool exeRead =
            countUtf8Runs("build/windows/x64/release/chess.exe", &exeRuns, &exeLongest, &exeOff);

        if (!exeRead)
        {
            std::printf("[enc] note: cannot open chess.exe for the artifact scan (skipped)\n");
        }
        else
        {
            std::printf("[enc] artifact scan of chess.exe: %d UTF-8 CJK run(s), "
                        "longest %d chars\n", exeRuns, exeLongest);

            if (exeRuns > 0)
            {
                ++failures;
                std::printf("[enc] FAIL: found UTF-8 CJK run(s) in the binary "
                            "(longest %d chars at 0x%zX)\n", exeLongest, exeOff);
                std::printf("[enc]   -> at least one translation unit has UTF-8 literals "
                            "while the app decodes them as GBK.\n");
                std::printf("[enc]   -> source-level flags look right, so this is almost "
                            "certainly STALE OBJECTS: run `xmake -r`\n");
            }
            else
            {
                std::printf("[enc] no UTF-8 CJK runs in the binary -> every translation "
                            "unit used GBK\n");
            }
        }
    }

    std::printf("[enc] %s (%d failures)\n", failures == 0 ? "ALL OK" : "HAS FAILURES",
                failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
//  字号 / 排版自检
// ---------------------------------------------------------------------------
//  起因：把 UI 字号调大之后，靠肉眼看截图判断"有没有溢出"是不可靠的
//  （截图经过缩放/压缩，一两个像素的越界根本看不出来，而且 CJK 文字宽度
//   随字体不同差异很大）。所以改成用文字度量 API 直接算：
//
//    * 菜单卡片：每条游戏/工具简介在卡片可用宽度下换行后，行数 × 行高
//      必须塞得进预留高度，且最后一行底部不能压到"变体"标签。
//    * 右侧面板：游戏简介、工具简介、等级说明同样按预留高度校验。
//    * 面板分组：用 MenuScene::optionsContentBottom() 走真实布局代码路径，
//      校验最后一组 chip 的底部不超过"重置配置"按钮的顶部。
//
//  全部用 theme 里的字号与 menu_layout 里的间距常量，所以这些常量一改，
//  自检结果就跟着变 —— 不会出现"自检抄了一份过时的数字"的情况。
// ---------------------------------------------------------------------------
int uiLayoutCheck()
{
    int failures = 0;
    int checked  = 0;

    auto report = [&](const char* what, int need, int avail) {
        ++checked;
        const bool ok = (need <= avail);
        if (!ok)
        {
            ++failures;
        }
        std::printf("[lay] %-28s need %3d  avail %3d  %s\n", what, need, avail,
                    ok ? "ok" : "<-- OVERFLOW");
    };

    // ---- 0. 宽窄字宽度一致性 ----
    //  换行算法在**宽字符**上工作（按字节切会劈开 GBK/UTF-8 多字节字符），
    //  而真正画到屏幕上的是窄字符 API。如果 toWide() 用的代码页与 EGE
    //  画窄字符串时用的代码页不一致，两边的字宽就不同 ——
    //  换行会按错误的宽度算，于是"以为放得下"的文字实际会溢出。
    //  这里直接比对同一个串在两条路径下的宽度，把这个前提钉死。
    {
        const char* samples[] = {
            "五子棋", "先连成五子者胜；可选黑棋禁手（长连 / 双四 / 双活三）",
            "大师", "11 层搜索 + VCF 连续冲四算杀；给胜率与主变",
            "象棋", "白棋", "19×19",
        };
        text::setFont(theme::kFontSmall, false, nullptr);
        for (const char* s : samples)
        {
            const std::wstring w = text::toWide(s);
            const int narrow = text::width(s, nullptr);
            const int wide   = static_cast<int>(textwidth(w.c_str(), nullptr));
            ++checked;
            const bool ok = (narrow == wide) && !w.empty();
            if (!ok)
            {
                ++failures;
            }
            std::printf("[lay] %-28s narrow %3d  wide %3d  chars %2d  %s\n",
                        "narrow/wide width parity", narrow, wide,
                        static_cast<int>(w.size()), ok ? "ok" : "<-- MISMATCH");
        }
    }

    // ---- 0. 有效字形尺寸 ----
    //  GDI 的 lfHeight 是**字符单元高度**（em + 行距），不是字形本身的尺寸。
    //  所以 setfont(17) 画出来的汉字实际只有 ~13px 见方 —— 这正是
    //  "字号看着比预期小"的原因。这里把每个档位的**真实**字宽/字高打出来，
    //  调字号时以此为准，而不是靠猜 lfHeight 与视觉大小的换算比。
    {
        struct Row
        {
            const char* name;
            int         px;
        };
        const Row rows[] = {
            {"kFontTitle", theme::kFontTitle},
            {"kFontBig", theme::kFontBig},
            {"kFontBase", theme::kFontBase},
            {"kFontSmall", theme::kFontSmall},
            {"kFontTiny", theme::kFontTiny},
        };
        for (const Row& r : rows)
        {
            text::setFont(r.px, false, nullptr);
            const int gw = textwidth("棋", nullptr);
            const int gh = textheight("\u68cb", nullptr);
            long long ascii = 0;
            {
                // ASCII 用一整串量，避免单字符取整误差
                text::setFont(r.px, false, nullptr);
                ascii = textwidth("MMMMMMMMMM", nullptr);
            }
            std::printf("[lay] %-28s px %2d -> glyph %2dx%2d, ascii/10 %.1f\n",
                        r.name, r.px, gw, gh, ascii / 10.0);
        }

        // lfHeight -> 实际字形宽度的对照曲线。
        // 换字体/改字号时用它重新挑值：想得到 N px 见方的汉字就选最接近 N 的 px。
        std::printf("[lay] px->glyph sweep:");
        for (int px = 13; px <= 34; ++px)
        {
            text::setFont(px, false, nullptr);
            char buf[8] = {0};
            const wchar_t w = L'\u68cb';
            WideCharToMultiByte(getcodepage(), 0, &w, 1, buf, sizeof(buf), nullptr, nullptr);
            std::printf(" %d:%d", px, textwidth(buf, nullptr));
        }
        std::printf("\n");
    }

    // ---- 1. 菜单卡片的简介文字 ----
    const int cardTextW = menu_layout::kCardW - 2 * menu_layout::kCardTextPad;

    text::setFont(theme::kFontSmall, false, nullptr);
    const int smallLine = theme::lineHeightFor(theme::kFontSmall);

    for (const GameDesc& d : allGames())
    {
        char tag[96];
        std::snprintf(tag, sizeof(tag), "card blurb: game %s", d.id);
        report(tag, text::wrappedHeight(d.blurb, cardTextW, smallLine, nullptr),
               menu_layout::kCardBlurbMax);
    }
    for (const ToolDesc& t : allTools())
    {
        char tag[96];
        std::snprintf(tag, sizeof(tag), "card blurb: tool %s", t.id);
        report(tag, text::wrappedHeight(t.blurb, cardTextW, smallLine, nullptr),
               menu_layout::kCardBlurbMax);
    }

    // 简介文字的底部不能压到卡片底部的"变体"标签
    const int cardTagTop = menu_layout::kCardH - menu_layout::kCardTagGap;
    report("card blurb bottom vs tag",
           menu_layout::kCardBlurbY + menu_layout::kCardBlurbMax, cardTagTop);

    // 卡片网格总高度不能溢出内容区
    const auto& games = allGames();
    const auto& tools = allTools();
    const int   nEntries = static_cast<int>(games.size() + tools.size());
    const int   nRows    = (nEntries + menu_layout::kCols - 1) / menu_layout::kCols;
    const int   gridH    = nRows * menu_layout::kCardH + (nRows - 1) * menu_layout::kCardGap;
    report("card grid height", gridH, kWindowHeight - theme::kTopBarH - 2 * menu_layout::kPad);

    // ---- 2. 右侧面板 ----
    const int panelInnerW = menu_layout::kPanelW - 2 * menu_layout::kPad;

    text::setFont(theme::kFontSmall, false, nullptr);
    for (const GameDesc& d : allGames())
    {
        char tag[96];
        std::snprintf(tag, sizeof(tag), "panel blurb: game %s", d.id);
        report(tag, text::wrappedHeight(d.blurb, panelInnerW, smallLine, nullptr),
               menu_layout::kPanelBlurbMax);
    }
    for (const ToolDesc& t : allTools())
    {
        char tag[96];
        std::snprintf(tag, sizeof(tag), "panel blurb: tool %s", t.id);
        report(tag, text::wrappedHeight(t.blurb, panelInnerW, smallLine, nullptr),
               menu_layout::kToolBlurbMax);
    }

    // 等级说明：用每一条等级 detail 的最坏情况（最长的那条）
    text::setFont(theme::kFontTiny, false, nullptr);
    const int tinyLine = theme::lineHeightFor(theme::kFontTiny);
    for (const GameDesc& d : allGames())
    {
        const std::vector<LevelDesc>& lv = d.levels.empty() ? defaultLevels() : d.levels;
        int worst = 0;
        for (const LevelDesc& l : lv)
        {
            worst = std::max(worst, text::wrappedHeight(l.detail, panelInnerW, tinyLine, nullptr));
        }
        char tag[96];
        std::snprintf(tag, sizeof(tag), "level detail: %s", d.id);
        report(tag, worst, menu_layout::kLevelDetailH);
    }

    // ---- 3. 面板分组是否压到底部按钮 ----
    // 这里复现 rebuildLayout 的面板内框：内容区高度 = 窗口 - 顶栏，
    // 面板高 = 内容区高 - 2*kPad，再 inset(kPad)。
    const int contentH  = kWindowHeight - theme::kTopBarH;
    const int contentY  = theme::kTopBarH;
    const Rect panel{0, contentY + menu_layout::kPad,
                     menu_layout::kPanelW, contentH - 2 * menu_layout::kPad};
    const Rect inner = panel.inset(menu_layout::kPad);

    // 与 rebuildLayout 里贴底按钮的公式保持一致
    const int startY = panel.bottom() - menu_layout::kPad - 48;
    const int resetY = startY - 42;

    for (const GameDesc& d : allGames())
    {
        const int bottom = MenuScene::optionsContentBottom(inner, d);
        char      tag[96];
        std::snprintf(tag, sizeof(tag), "panel stack: %s", d.id);
        report(tag, bottom, resetY - 8);
    }

    // ---- 4. 图集画廊 ----
    //  精灵名来自 chesses.txt，长度不可控；网格总尺寸也可能超出一屏。
    //  两者都要盯住：换素材（200/300/400）或改字号时最容易在这里溢出。
    {
        const Atlas& atlas = App::inst().atlas();
        const auto&  names = atlas.names();

        text::setFont(theme::kFontTiny, true, nullptr);
        int     worstName = 0;
        const char* worstNameStr = "";
        for (size_t i = 0; i < names.size(); ++i)
        {
            char line[128];
            std::snprintf(line, sizeof(line), "#%d  %s", static_cast<int>(i),
                          names[i].c_str());
            const int w = text::width(line, nullptr);
            if (w > worstName)
            {
                worstName    = w;
                worstNameStr = names[i].c_str();
            }
        }
        std::printf("[lay] atlas sprites %d, longest title '%s'\n",
                    static_cast<int>(names.size()), worstNameStr);
        report("atlas gallery title", worstName, atlas_layout::kTitleW);

        const int nCols = atlas_layout::kCols;
        const int nRows = (static_cast<int>(names.size()) + nCols - 1) / nCols;
        const int gridH = nRows * atlas_layout::kStepY;
        const int gridW = nCols * atlas_layout::kStepX;
        report("atlas gallery grid h", gridH,
               kWindowHeight - theme::kTopBarH - 48);
        report("atlas gallery grid w", gridW, kWindowWidth - 48);
    }

    std::printf("[lay] %s (%d checks, %d failures)\n",
                failures == 0 ? "ALL OK" : "HAS FAILURES", checked, failures);
    return failures == 0 ? 0 : 1;
}

} // namespace chess
