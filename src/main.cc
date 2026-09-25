// ============================================================================
//  chessgame —— 程序入口
// ----------------------------------------------------------------------------
//  命令行：
//     chess                  启动图形界面（主菜单 -> 对局 / 工具）
//     chess --atlas-dump     打印精灵表并输出带标注的对照图到 build/atlas_dump.png
//     chess --gfx-selftest   自检 EGE 离屏绘制路径（alpha 处理），输出到 build/
//     chess --help           显示帮助
//
//  控制台输出一律用 ASCII：源码是按 GBK 生成窄字符串的，而 VS Code 终端是
//  UTF-8，直接打中文会乱码。界面文案才是中文（见 core/config.h）。
// ============================================================================

#include <graphics.h>

#include "app/app.h"
#include "app/atlas_scene.h"
#include "app/menu_scene.h"
#include "app/selftest.h"
#include "core/config.h"
#include "core/stopwatch.h"
#include "games/registry.h"
#include "ui/gfx_util.h"

#include <cstdio>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

enum class Mode
{
    Run = 0,
    AtlasDump,
    GfxSelfTest,
    FlipTest,
    GomokuTest,
    PerftTest,
    DumpBoard,
    GoTest,
    AiBudget,
    XqPiece,
    Encoding,
    UiLayout,
    WinInfo,
    HitTest,
    Shot,
};

// 截图目标里“game:<id>”形式的解析
bool isGameShotTarget(const std::string& t) { return t.rfind("game:", 0) == 0; }

void printUsage()
{
    std::printf("chessgame - 2D board game collection (EGE)\n");
    std::printf("  chess                  launch the GUI (menu -> game / tools)\n");
    std::printf("  chess --atlas-dump     dump the sprite table + annotated contact sheet\n");
    std::printf("  chess --gfx-selftest   probe EGE offscreen drawing paths (alpha handling)\n");
    std::printf("  chess --flip-test      verify the flip-puzzle GF(2) solver against brute force\n");
    std::printf("  chess --gomoku-test    verify gomoku rules, forbidden moves and AI behaviour\n");
    std::printf("  chess --perft          verify xiangqi move generation (perft 44/1920/79666)\n");
    std::printf("  chess --dump-board     print piece -> atlas sprite mapping for xiangqi\n");
    std::printf("  chess --go-test        verify go rules (capture / suicide / ko / scoring)\n");
    std::printf("  chess --ai-budget      verify later searches still use their time budget\n");
    std::printf("  chess --xq-piece       verify xiangqi pieces render base + glyph layering\n");
    std::printf("  chess --encoding       verify the chinese text encoding chain end-to-end\n");
    std::printf("  chess --ui-layout      verify enlarged UI text still fits its containers\n");
    std::printf("  chess --wininfo        report window client size vs canvas size (click accuracy)\n");
    std::printf("  chess --hit-test       verify board drawing rect vs hit-test mapping\n");
    std::printf("  chess --shot <target>  render one frame offscreen to build/shot_<target>.png\n");
    std::printf("                         targets: menu | atlas | atlas-cal | game:<id>\n");
    std::printf("  chess --help           show this help\n");
}

// 离屏渲染一帧并存盘。用来在无人值守的情况下核对界面布局
// （比手动截图可靠得多，也不依赖窗口是否被遮挡）。
int renderShot(chess::App& app, const std::string& target)
{
    std::unique_ptr<chess::Scene> scene;
    std::vector<key_msg>          syntheticKeys;

    if (target == "menu")
    {
        scene = std::make_unique<chess::MenuScene>();
    }
    else if (target == "atlas")
    {
        scene = std::make_unique<chess::AtlasScene>();
    }
    else if (target == "atlas-cal")
    {
        scene = std::make_unique<chess::AtlasScene>();
        // 用与真实输入相同的一条路径切到校准视图
        syntheticKeys.push_back(key_msg{key_f1, key_msg_down, 0});
    }
    else if (isGameShotTarget(target))
    {
        const std::string id = target.substr(5);
        const chess::GameDesc* d = chess::findGame(id.c_str());
        if (!d)
        {
            std::printf("[shot] unknown game id: %s\n", id.c_str());
            return 2;
        }

        chess::GameConfig cfg;
        // 取一个中等尺寸的棋盘，让截图既有内容又能看出布局
        if (d->boardPresets.size() > 1)
        {
            cfg.cols = d->boardPresets[1].cols;
            cfg.rows = d->boardPresets[1].rows;
        }
        else if (!d->boardPresets.empty())
        {
            cfg.cols = d->boardPresets[0].cols;
            cfg.rows = d->boardPresets[0].rows;
        }
        cfg.variant  = 2;   // 困难：局面更有内容
        cfg.aiLevel  = 5;   // 最高等级：把全部提示字段都填上
        cfg.seed     = 0xC0FFEEull;

        scene = chess::makeGameScene(*d, cfg);
        if (!scene)
        {
            std::printf("[shot] game has no factory: %s\n", id.c_str());
            return 2;
        }
    }
    else
    {
        std::printf("[shot] unknown target: %s\n", target.c_str());
        return 2;
    }

    app.pushScene(std::move(scene));

    // 让虚拟鼠标落在一个有意义的位置，呈现 hover 反馈
    app.setVirtualMouse(240, 160);

    for (const key_msg& k : syntheticKeys)
    {
        app.dispatchSyntheticKey(k);
    }

    // 推几帧 update，并等后台分析出结果，让高级提示能在截图里出现
    const chess::Stopwatch sw;
    for (int i = 0; i < 200; ++i)
    {
        app.updateForShot(16.0);
        if (i > 2 && !app.ai().running() && app.sceneAnalysisSettled())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)sw;

    PIMAGE frame = newimage(chess::kWindowWidth, chess::kWindowHeight);
    if (!frame)
    {
        std::printf("[shot] newimage failed\n");
        return 1;
    }

    app.renderFrame(frame, 16.0);

    // 离屏图走 GDI 的图元不会写 alpha，不补齐的话存出来是一片黑
    chess::makeImageOpaque(frame);

    // 文件名里不能留 ':' —— Windows 上它是 NTFS 备用数据流分隔符，
    // 会让 "shot_game:flip.png" 变成一个 ADS 而不是真正的文件
    // （表现为 savepng 返回成功但磁盘上没有那个文件）。
    std::string safe = target;
    for (char& ch : safe)
    {
        if (ch == ':' || ch == '/' || ch == '\\' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|')
        {
            ch = '_';
        }
    }

    const std::string out = "build/shot_" + safe + ".png";
    const bool ok = savepng(frame, out.c_str(), false) == grOk;
    delimage(frame);

    std::printf("[shot] %s: %s\n", ok ? "written" : "FAILED", out.c_str());
    return ok ? 0 : 2;
}

} // namespace

int main(int argc, char* argv[])
{
    Mode        mode = Mode::Run;
    std::string shotTarget;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--atlas-dump")
        {
            mode = Mode::AtlasDump;
        }
        else if (a == "--gfx-selftest")
        {
            mode = Mode::GfxSelfTest;
        }        else if (a == "--flip-test")
        {
            mode = Mode::FlipTest;
        }
        else if (a == "--gomoku-test")
        {
            mode = Mode::GomokuTest;
        }
        else if (a == "--perft")
        {
            mode = Mode::PerftTest;
        }
        else if (a == "--dump-board")
        {
            mode = Mode::DumpBoard;
        }
        else if (a == "--go-test")
        {
            mode = Mode::GoTest;
        }
        else if (a == "--ai-budget")
        {
            mode = Mode::AiBudget;
        }
        else if (a == "--xq-piece")
        {
            mode = Mode::XqPiece;
        }
        else if (a == "--encoding")
        {
            mode = Mode::Encoding;
        }
        else if (a == "--ui-layout")
        {
            mode = Mode::UiLayout;
        }
        else if (a == "--wininfo")
        {
            mode = Mode::WinInfo;
        }
        else if (a == "--hit-test")
        {
            mode = Mode::HitTest;
        }
        else if (a == "--shot")
        {
            mode = Mode::Shot;
            if (i + 1 >= argc)
            {
                std::printf("--shot needs a target (menu | atlas | atlas-cal | game:<id>)\n");
                return 2;
            }
            shotTarget = argv[++i];
        }
        else if (a == "-h" || a == "--help")
        {
            printUsage();
            return 0;
        }
        else
        {
            std::printf("unknown argument: %s\n", a.c_str());
            printUsage();
            return 2;
        }
    }

    const bool headless = (mode != Mode::Run);

    // 手动刷新模式；无界面模式用隐藏窗口（只画到离屏 IMAGE 后存盘 / 采样）
    setinitmode(static_cast<int>(INIT_RENDERMANUAL) |
                (headless ? static_cast<int>(INIT_HIDE) : 0));
    initgraph(chess::kWindowWidth, chess::kWindowHeight, getinitmode());

    // 显式设置代码页：不依赖 EGE 的隐含默认值（零初始化 = EGE_CODEPAGE_ANSI），
    // 这样窄字符串字面量（编译期已转成 GBK 字节）才能被正确解析
    setcodepage(CHESS_TEXT_CODEPAGE);

    chess::App& app = chess::App::inst();

    std::string err;
    if (!app.init(&err))
    {
        std::printf("[fatal] init failed: %s\n", err.c_str());
        closegraph();
        return 1;
    }

    int rc = 0;
    switch (mode)
    {
    case Mode::GfxSelfTest:
        rc = chess::gfxSelfTest();
        break;

    case Mode::FlipTest:
        rc = chess::flipSolverTest();
        break;

    case Mode::GomokuTest:
        rc = chess::gomokuTest();
        break;

    case Mode::PerftTest:
        rc = chess::xiangqiPerft();
        break;

    case Mode::DumpBoard:
        rc = chess::dumpBoard();
        break;

    case Mode::GoTest:
        rc = chess::goRulesTest();
        break;

    case Mode::AiBudget:
        rc = chess::aiBudgetTest();
        break;

    case Mode::XqPiece:
        rc = chess::xiangqiPieceRenderTest();
        break;

    case Mode::Encoding:
        rc = chess::encodingTest();
        break;

    case Mode::UiLayout:
        rc = chess::uiLayoutCheck();
        break;

    case Mode::WinInfo:
        rc = chess::windowInfoCheck();
        break;

    case Mode::HitTest:
        rc = chess::hitTestCheck();
        break;

    case Mode::AtlasDump:
        rc = chess::atlasDump();
        break;

    case Mode::Shot:
        rc = renderShot(app, shotTarget);
        break;

    case Mode::Run:
    default:
        app.pushScene(std::make_unique<chess::MenuScene>());
        app.run();
        break;
    }

    closegraph();
    return rc;
}
