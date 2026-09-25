#include "app/game_scene.h"

#include "app/app.h"
#include "core/config.h"
#include "games/registry.h"
#include "ui/painter.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>

namespace chess {

namespace {

constexpr int kBtnH   = 48;
constexpr int kBtnGap = 8;

} // namespace

GameScene::GameScene(std::unique_ptr<IGame> game, std::unique_ptr<IAI> ai, GameConfig cfg)
    : game_(std::move(game)), ai_(std::move(ai)), cfg_(cfg)
{
    hintLevel_ = std::clamp(cfg_.aiLevel, 1, 5);
}

const char* GameScene::name() const
{
    return game_ ? game_->desc().name : "对局";
}

void GameScene::onEnter(App& app)
{
    game_->setup(cfg_);
    boardArea_ = Rect{0, app.content().y,
                      kWindowWidth - theme::kHudW, app.content().h};
    bv_.layout(game_->boardSpec(), boardArea_);

    app.toast(std::string("新对局：") + game_->variantName());

    // 局面一变就会触发分析；这里不必显式启动
    selected_ = Coord{};
    hover_    = Coord{};
}

void GameScene::onExit(App&)
{
    // App 会负责取消 AI 线程（keepAiAlive() 返回 false）
}

bool GameScene::isHumanTurn() const
{
    if (game_->isOver())
    {
        return false;
    }
    if (cfg_.humanVsHuman)
    {
        return true;
    }
    // 单人解谜类：始终由玩家操作，需要代按时用"AI 替我走"按钮
    if (!game_->hasSides())
    {
        return true;
    }
    return game_->sideToMove() == (cfg_.humanSide == 2 ? Side::Second : Side::First);
}

bool GameScene::needsAnalysis() const
{
    if (!ai_ || game_->isOver())
    {
        return false;
    }
    return !hasAnalysis_ || analyzedKey_ != game_->stateKey();
}

void GameScene::startAnalysis(App& app)
{
    // AI 需要在局面的深拷贝上搜索，避免与 UI 线程共享状态
    auto pos = game_->clone();
    analyzedKey_ = game_->stateKey();
    hasAnalysis_ = false;
    aiHint_      = HintData{};
    app.ai().start(std::move(pos), std::unique_ptr<IAI>(ai_->cloneForThread()),
                   hintLevel_, defaultTimeBudgetMs(hintLevel_));
}

void GameScene::onAnalysisDone(App& app, const AiOutcome& out, int elapsedMs)
{
    lastElapsedMs_ = elapsedMs;
    aiHint_        = out.hint;
    hasAnalysis_   = true;

    // 轮到 AI：用搜索结果走棋
    if (!isHumanTurn() && out.hasMove && game_->isLegal(out.move))
    {
        applyMove(app, out.move);
    }
}

void GameScene::applyMove(App& app, const Move& m)
{
    if (!game_->apply(m))
    {
        app.toast("这一手不合法");
        return;
    }

    selected_ = Coord{};

    // 局面变了，丢弃旧分析；下一帧 needsAnalysis() 会重启搜索
    hasAnalysis_ = false;

    if (game_->isOver() && !announcedOver_)
    {
        announcedOver_ = true;
        gameOverMs_    = 0.0;
        app.toast(game_->statusText(), 4000);
    }
}

void GameScene::undoOne()
{
    if (game_->undo())
    {
        hasAnalysis_   = false;
        announcedOver_ = false;
        selected_      = Coord{};
    }
}

void GameScene::undoHumanTurn(App& app)
{
    (void)app;
    if (game_->isOver())
    {
        // 已经结束：先撤掉最后这一手，让局面回到可继续的状态
        undoOne();
        announcedOver_ = false;
        return;
    }

    // 一直撤到"轮到人走"为止（通常撤 2 手：AI 的 + 人的）
    for (int i = 0; i < 4; ++i)
    {
        undoOne();
        if (isHumanTurn())
        {
            break;
        }
    }
}

void GameScene::restart(App& app)
{
    game_->setup(cfg_);
    hasAnalysis_   = false;
    announcedOver_ = false;
    gameOverMs_    = 0.0;
    selected_      = Coord{};
    hover_         = Coord{};
    bv_.layout(game_->boardSpec(), boardArea_);
    app.toast("已重新开始");
}

// ---------------------------------------------------------------------------
//  update / draw
// ---------------------------------------------------------------------------

void GameScene::update(App& app, double dtMs)
{
    if (announcedOver_)
    {
        gameOverMs_ += dtMs;
    }

    // 取回后台搜索结果
    AiOutcome out;
    int       elapsed = 0;
    if (app.ai().poll(out, &elapsed))
    {
        onAnalysisDone(app, out, elapsed);
    }

    // 局面变了（或还没分析过）就重启分析
    if (needsAnalysis() && !app.ai().running())
    {
        startAnalysis(app);
    }

    rebuildLayout(app);

    // hover 反馈
    hover_ = bv_.hitTest(app.mouseX(), app.mouseY());
    hoverBtn_ = -1;
    for (int i = 0; i < static_cast<int>(Btn::Count); ++i)
    {
        if (btnRects_[i].contains(app.mouseX(), app.mouseY()))
        {
            hoverBtn_ = i;
            break;
        }
    }
}

void GameScene::rebuildLayout(const App& app)
{
    hudRect_ = Rect{app.content().right() - theme::kHudW, app.content().y,
                    theme::kHudW, app.content().h};

    const Rect inner = hudRect_.inset(16);

    // 底部按钮排成两列
    const int colW = (inner.w - kBtnGap) / 2;
    int       y    = hudRect_.bottom() - 16 - kBtnH;

    const char* labels[static_cast<int>(Btn::Count)] = {
        "悔棋", "重新开始", "提示 开/关", "AI 替我走一步", "停一手",
    };

    btnRects_[static_cast<int>(Btn::Undo)]     = Rect{inner.x, y, colW, kBtnH};
    btnRects_[static_cast<int>(Btn::Restart)]  = Rect{inner.x + colW + kBtnGap, y, colW, kBtnH};
    y -= kBtnH + kBtnGap;
    btnRects_[static_cast<int>(Btn::ToggleHint)] = Rect{inner.x, y, colW, kBtnH};
    btnRects_[static_cast<int>(Btn::AiMove)]     = Rect{inner.x + colW + kBtnGap, y, colW, kBtnH};
    y -= kBtnH + kBtnGap;
    btnRects_[static_cast<int>(Btn::Pass)] = Rect{inner.x, y, inner.w, kBtnH};

    (void)labels;
}

// ---------------------------------------------------------------------------
//  绘制
// ---------------------------------------------------------------------------

void GameScene::drawHintOverlay(const App& app, PIMAGE img)
{
    (void)app;
    if (!showHint_)
    {
        return;
    }

    // L1：所有可走 / 不可走
    const HintData basic = game_->basicHint(hintLevel_);
    for (const HintCell& c : basic.cells)
    {
        bv_.drawHint(img, c.coord, c.kind, c.weight);
    }

    // L2+：搜索给出的推荐 / 有利 / 危险
    if (hasAnalysis_)
    {
        for (const HintCell& c : aiHint_.cells)
        {
            bv_.drawHint(img, c.coord, c.kind, c.weight);
        }
    }

    // 象棋这类需要"起点 -> 终点"的游戏，选中后把可去的点标出来
    if (game_->needsFromTo() && selected_.valid())
    {
        for (const Move& m : game_->legalMoves())
        {
            if (m.from == selected_)
            {
                bv_.drawHint(img, m.to, HintKind::Legal, 1.0f);
            }
        }
    }
}

void GameScene::drawBoardArea(App& app, PIMAGE img)
{
    (void)app;

    ui::fillBox(img, boardArea_, theme::kBg);

    bv_.drawBoardBase(img);
    bv_.drawGrid(img);

    // 绘制上下文：把图集与当前提示等级交给游戏，
    // 使游戏能够"高阶多给信息"（例如把解法顺序写到棋盘上）
    DrawContext ctx;
    ctx.atlas     = &app.atlas();
    ctx.hintLevel = showHint_ ? hintLevel_ : 0;
    ctx.showHint  = showHint_;
    ctx.hover     = hover_;
    ctx.selected  = selected_;

    game_->drawDecorations(bv_, ctx, img);

    // 棋子
    for (int y = 0; y < bv_.rows(); ++y)
    {
        for (int x = 0; x < bv_.cols(); ++x)
        {
            game_->drawCell(bv_, Coord{x, y}, ctx, img);
        }
    }

    drawHintOverlay(app, img);

    // 最后一手
    if (game_->hasLastMove())
    {
        const Move m = game_->lastMove();
        const Coord c = m.to.valid() ? m.to : m.from;
        if (c.valid())
        {
            const bool light = game_->sideToMove() == Side::First;
            bv_.drawLastMove(img, c, EGERGB(0xFF, 0x5A, 0x3C),
                             light ? EGERGB(0xFF, 0xFF, 0xFF) : EGERGB(0x10, 0x10, 0x10));
        }
    }

    if (selected_.valid())
    {
        bv_.drawSelection(img, selected_, theme::kAccent);
    }
    else if (hover_.valid() && isHumanTurn())
    {
        bv_.drawHover(img, hover_, theme::kAccentDim);
    }

    game_->drawOverlay(bv_, ctx, img);
}

void GameScene::drawHud(App& app, PIMAGE img)
{
    ui::panel(img, hudRect_, theme::kPanel, theme::kLine, theme::kRadius);

    const Rect inner = hudRect_.inset(16);
    int        y     = inner.y + 2;

    // 次要信息行（kFontTiny）的行高：从 theme 推导，字号改了自动跟着变
    const int tinyLh = theme::lineHeightFor(theme::kFontTiny);

    // ---- 当前行棋方 ----
    {
        const Rect row{inner.x, y, inner.w, 56};
        ui::fillRound(img, row, theme::kRadiusSm, theme::kPanelAlt);

        const Side side = game_->sideToMove();
        const int  sz   = 38;

        const bool hasSides = game_->hasSides();

        // 能用图集棋子就用（黑白棋/五子棋/围棋），否则画一个纯色圆盘（象棋）
        const char* sprite = hasSides ? game_->pieceSprite(side) : nullptr;
        if (sprite && app.atlas().has(sprite))
        {
            app.atlas().draw(sprite, row.x + 8, row.y + (row.h - sz) / 2, sz, img);
        }
        else
        {
            // 单人解谜类：用一个"棋盘格"图标代替棋子
            const int cx = row.x + 8 + sz / 2;
            const int cy = row.y + row.h / 2;
            if (!hasSides)
            {
                setfillcolor(theme::kAccentDim, img);
                fillroundrect(cx - 13, cy - 13, cx + 13, cy + 13, 5, 5, img);
                setcolor(theme::kAccent, img);
                setlinewidth(2.0f, img);
                rectangle(cx - 13, cy - 13, cx + 13, cy + 13, img);
                setlinewidth(1.0f, img);
            }
            else
            {
                const color_t fill = (side == Side::First) ? theme::kSideFirst : theme::kSideSecond;
                setfillcolor(fill, img);
                fillellipse(cx, cy, sz / 2 - 2, sz / 2 - 2, img);
                setcolor(side == Side::First ? theme::kTextFaint : theme::kLine, img);
                circle(cx, cy, sz / 2 - 2, img);
            }
        }

        text::setFont(theme::kFontSmall, false, img);
        setcolor(theme::kTextFaint, img);
        text::draw(game_->isOver() ? "已完成" : (hasSides ? "当前行棋" : "单人对局"),
                   row.x + 58, row.y + 7, img);

        text::setFont(theme::kFontBig, true, img);
        setcolor(theme::kText, img);
        text::draw(hasSides ? game_->sideName(side) : game_->variantName().c_str(),
                   row.x + 58, row.y + 27, img);

        y = row.bottom() + 12;
    }

    // ---- 状态 ----
    {
        text::setFont(theme::kFontSmall, false, img);
        setcolor(game_->isOver() ? theme::kWarn : theme::kTextDim, img);
        const std::string st = game_->statusText();
        text::drawIn(st.c_str(), Rect{inner.x, y, inner.w, 28}, Align::Left, VAlign::Top, img);
        y += 32;
    }

    const std::string score = game_->scoreText();
    if (!score.empty())
    {
        text::setFont(theme::kFontSmall, true, img);
        setcolor(theme::kText, img);
        text::drawIn(score.c_str(), Rect{inner.x, y, inner.w, 28}, Align::Left, VAlign::Top, img);
        y += 30;
    }

    ui::divider(img, Rect{inner.x, y, inner.w, 1}, theme::kLineSoft);
    y += 14;

    // ---- 分析区 ----
    {
        const bool thinking = app.ai().running();
        const AiProgress& pr = app.ai().progress();

        text::setFont(theme::kFontSmall, true, img);
        setcolor(theme::kText, img);

        char head[128];
        const char* eng = ai_ ? ai_->engineName() : "无 AI";
        std::snprintf(head, sizeof(head), "分析  %s  %s", levelShortName(hintLevel_), eng);
        text::drawIn(head, Rect{inner.x, y, inner.w, 28}, Align::Left, VAlign::Top, img);
        y += 32;

        text::setFont(theme::kFontTiny, false, img);
        setcolor(thinking ? theme::kAccent : theme::kTextFaint, img);

        char line[192];
        if (thinking)
        {
            std::snprintf(line, sizeof(line), "思考中 %d ms   深度 %d   节点 %lld",
                          app.ai().elapsedMs(), pr.depth.load(), pr.nodes.load());
        }
        else if (hasAnalysis_)
        {
            std::snprintf(line, sizeof(line), "已完成 %d ms   深度 %d   节点 %lld",
                          lastElapsedMs_, aiHint_.depth, aiHint_.nodes);
        }
        else
        {
            std::snprintf(line, sizeof(line), "等待分析…");
        }
        text::drawIn(line, Rect{inner.x, y, inner.w, 24}, Align::Left, VAlign::Top, img);
        y += 30;

        // 胜率条（L4/L5 才有）
        if (hasAnalysis_ && aiHint_.hasWinRate)
        {
            const double wr = std::clamp(aiHint_.winRate, 0.0, 1.0);
            const Rect   bar{inner.x, y, inner.w, 16};
            ui::meterSplit(img, bar, wr, theme::kSideFirst, theme::kSideSecond);

            std::snprintf(line, sizeof(line), "胜率 %.1f%%   %.1f%%", wr * 100.0, (1.0 - wr) * 100.0);
            text::setFont(theme::kFontTiny, false, img);
            setcolor(theme::kTextDim, img);
            text::drawIn(line, Rect{inner.x, y + 22, inner.w, 22}, Align::Left, VAlign::Top, img);
            y += 50;
        }
        else if (hasAnalysis_ && aiHint_.hasEval)
        {
            std::snprintf(line, sizeof(line), "评估 %s", aiHint_.evalText.c_str());
            setcolor(theme::kTextDim, img);
            text::drawIn(line, Rect{inner.x, y, inner.w, 24}, Align::Left, VAlign::Top, img);
            y += 28;
        }

        // 主变
        if (hasAnalysis_ && !aiHint_.lines.empty())
        {
            for (const HintLine& hl : aiHint_.lines)
            {
                if (hl.moves.empty())
                {
                    continue;
                }
                text::setFont(theme::kFontTiny, false, img);
                setcolor(theme::kTextFaint, img);
                y += text::drawWrapped(hl.label.c_str(), Rect{inner.x, y, inner.w, tinyLh * 2},
                                       Align::Left, tinyLh, img);
                // 这里只显示"有几手"，具体坐标由棋盘上的箭头体现
                std::snprintf(line, sizeof(line), "%d 手", static_cast<int>(hl.moves.size()));
                setcolor(theme::kTextDim, img);
                text::drawIn(line, Rect{inner.x, y, inner.w, tinyLh}, Align::Left, VAlign::Top, img);
                y += tinyLh + 4;
            }
        }

        // 文字结论
        if (hasAnalysis_)
        {
            for (const std::string& n : aiHint_.notes)
            {
                text::setFont(theme::kFontTiny, false, img);
                setcolor(theme::kTextDim, img);
                y += text::drawWrapped(n.c_str(), Rect{inner.x, y, inner.w, tinyLh * 3},
                                       Align::Left, tinyLh, img);
                y += 2;
            }
        }
    }

    // ---- 底部按钮 ----
    const bool over = game_->isOver();

    auto drawBtn = [&](Btn b, const char* label, bool enabled,
                       ui::State base = ui::State::Normal) {
        const Rect& r = btnRects_[static_cast<int>(b)];
        ui::State st = base;
        if (!enabled)
        {
            st = ui::State::Disabled;
        }
        else if (hoverBtn_ == static_cast<int>(b))
        {
            st = ui::State::Hover;
        }
        ui::button(img, r, label, st);
    };

    drawBtn(Btn::Undo, "悔棋", game_->canUndo());
    drawBtn(Btn::Restart, "重新开始", true);
    drawBtn(Btn::ToggleHint, showHint_ ? "提示：开" : "提示：关", true,
            showHint_ ? ui::State::Selected : ui::State::Normal);
    drawBtn(Btn::AiMove, "AI 替我走", hasAnalysis_ && !over,
            ui::State::Selected);
    drawBtn(Btn::Pass, game_->needsPassButton() ? "停一手 (Pass)" : "（无需停一手）",
            game_->needsPassButton() && !over);
}

void GameScene::draw(App& app, PIMAGE img)
{
    drawBoardArea(app, img);
    drawHud(app, img);
}

// ---------------------------------------------------------------------------
//  输入
// ---------------------------------------------------------------------------

void GameScene::onCellClick(App& app, Coord c)
{
    if (game_->isOver() || !isHumanTurn())
    {
        return;
    }

    if (game_->needsFromTo())
    {
        if (selected_.valid())
        {
            const Move m{selected_, c};
            if (game_->isLegal(m))
            {
                applyMove(app, m);
                return;
            }
        }
        selected_ = game_->selectableFrom(c, game_->sideToMove()) ? c : Coord{};
        return;
    }

    Move m{};
    m.to = c;
    if (game_->isLegal(m))
    {
        applyMove(app, m);
    }
}

bool GameScene::onMouse(App& app, const mouse_msg& m)
{
    if (!m.is_down() || !m.is_left())
    {
        return false;
    }

    // 按钮
    if (hoverBtn_ >= 0)
    {
        switch (static_cast<Btn>(hoverBtn_))
        {
        case Btn::Undo:
            undoHumanTurn(app);
            app.toast("已悔棋");
            return true;
        case Btn::Restart:
            restart(app);
            return true;
        case Btn::ToggleHint:
            showHint_ = !showHint_;
            return true;
        case Btn::AiMove:
            if (hasAnalysis_ && aiHint_.complete)
            {
                // 用 AI 分析出的那一手替人走
                for (const HintCell& hc : aiHint_.cells)
                {
                    if (hc.kind == HintKind::Recommended)
                    {
                        Move mv{};
                        mv.to = hc.coord;
                        if (!game_->isLegal(mv))
                        {
                            mv.from = selected_;
                        }
                        if (game_->isLegal(mv))
                        {
                            applyMove(app, mv);
                            return true;
                        }
                    }
                }
            }
            // 没有现成结果就直接让 AI 走一手（把当前局面交给引擎）
            if (!app.ai().running() && ai_)
            {
                auto pos = game_->clone();
                aiHint_      = HintData{};
                hasAnalysis_ = false;
                analyzedKey_ = game_->stateKey();
                app.ai().start(std::move(pos), std::unique_ptr<IAI>(ai_->cloneForThread()),
                               hintLevel_, defaultTimeBudgetMs(hintLevel_));
                app.toast("让 AI 走一手…");
            }
            return true;
        case Btn::Pass:
            if (game_->needsPassButton() && !game_->isOver())
            {
                Move mv{};
                if (game_->isLegal(mv))
                {
                    applyMove(app, mv);
                }
            }
            return true;
        default:
            break;
        }
    }

    // 棋盘
    if (boardArea_.contains(m.x, m.y))
    {
        const Coord c = bv_.hitTest(m.x, m.y);
        if (c.valid())
        {
            onCellClick(app, c);
            return true;
        }
    }

    return false;
}

bool GameScene::onKey(App& app, const key_msg& k)
{
    switch (k.key)
    {
    case key_esc:
        app.popScene();
        return true;

    case 'U':
    case 'u':
        undoHumanTurn(app);
        app.toast("已悔棋");
        return true;

    case 'R':
    case 'r':
        restart(app);
        return true;

    case 'H':
    case 'h':
        showHint_ = !showHint_;
        app.toast(showHint_ ? "提示：开" : "提示：关");
        return true;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    {
        hintLevel_ = k.key - '0';
        hasAnalysis_ = false;   // 换等级要重新分析
        char buf[64];
        std::snprintf(buf, sizeof(buf), "AI 等级 -> %s（%s）", levelShortName(hintLevel_),
                      hintLevel_ <= 2 ? "只要可走点与危险预警"
                                      : (hintLevel_ == 3 ? "推荐着法 + 评估"
                                                         : "主变 + 深度"));
        app.toast(buf);
        return true;
    }

    case key_space:
        if (game_->needsPassButton() && !game_->isOver())
        {
            Move mv{};
            if (game_->isLegal(mv))
            {
                applyMove(app, mv);
            }
        }
        return true;

    default:
        break;
    }
    return false;
}

std::string GameScene::topBarInfo() const
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s | %s | 手数 %d | 等级 %s | U悔棋 R重开 H提示 1-5调等级",
                  game_->desc().name, game_->variantName().c_str(),
                  game_->moveCount(), levelShortName(hintLevel_));
    return buf;
}

// ---------------------------------------------------------------------------
//  工厂
// ---------------------------------------------------------------------------

std::unique_ptr<Scene> makeGameScene(const GameDesc& desc, GameConfig cfg)
{
    if (!desc.create)
    {
        return nullptr;
    }

    std::unique_ptr<IGame> game = desc.create();
    std::unique_ptr<IAI>   ai;
    if (desc.createAi)
    {
        ai = desc.createAi();
    }
    return std::make_unique<GameScene>(std::move(game), std::move(ai), cfg);
}

} // namespace chess
