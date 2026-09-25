#include "games/registry.h"

#include "ai/flip_ai.h"
#include "ai/go_ai.h"
#include "ai/gomoku_ai.h"
#include "ai/xiangqi_ai.h"
#include "games/flip_puzzle.h"
#include "games/go.h"
#include "games/gomoku.h"
#include "games/xiangqi.h"

#include <string>

namespace chess {

// ---------------------------------------------------------------------------
//  游戏注册表
// ---------------------------------------------------------------------------
//  每个游戏在自己的头文件里提供一个 xxxDesc() 返回 GameDesc（含工厂函数）。
//  这里集中登记，菜单按此顺序展示；没有实现的游戏不登记。
//
//  约定：新增游戏时
//    1) 在 games/ 下加 <game>.h/.cc，提供 `<game>Desc()` 与工厂
//    2) 在下方列表里加一行
//    3) 在 ai/ 下加对应引擎（若该游戏需要搜索）
// ---------------------------------------------------------------------------
const std::vector<GameDesc>& allGames()
{
    static const std::vector<GameDesc> games = [] {
        std::vector<GameDesc> list;

        // 翻转棋（黑白棋的 flip 变体）：单人解谜 + 精确求解器
        {
            GameDesc d = FlipPuzzleGame{}.desc();
            // AI 工厂要在游戏层之外填，避免 games/ 反向依赖 ai/
            d.createAi = []() -> std::unique_ptr<IAI> { return std::make_unique<FlipPuzzleAi>(); };
            list.push_back(std::move(d));
        }

        // 五子棋
        {
            GameDesc d = GomokuGame{}.desc();
            d.createAi = []() -> std::unique_ptr<IAI> { return std::make_unique<GomokuAi>(); };
            list.push_back(std::move(d));
        }

        // 象棋
        {
            GameDesc d = XiangqiGame{}.desc();
            d.createAi = []() -> std::unique_ptr<IAI> { return std::make_unique<XiangqiAi>(); };
            list.push_back(std::move(d));
        }

        // 围棋
        {
            GameDesc d = GoGame{}.desc();
            d.createAi = []() -> std::unique_ptr<IAI> { return std::make_unique<GoAi>(); };
            list.push_back(std::move(d));
        }

        return list;
    }();
    return games;
}

const std::vector<ToolDesc>& allTools()
{
    static const std::vector<ToolDesc> tools = {
        {"atlas", "图集查看器", "逐格核对 res/chesses100.png 的精灵映射与像素矩形"},
    };
    return tools;
}

const GameDesc* findGame(const char* id)
{
    if (!id)
    {
        return nullptr;
    }
    for (const GameDesc& d : allGames())
    {
        if (std::string(d.id) == id)
        {
            return &d;
        }
    }
    return nullptr;
}

const std::vector<LevelDesc>& defaultLevels()
{
    // 提示层级约定（各游戏的具体内容不同，但"每一级多给什么"是一致的）：
    //   L1 可走/不可走
    //   L2 加危险与威胁预警
    //   L3 加推荐着法与评估分
    //   L4 加多条主变与搜索深度
    //   L5 加胜率与更深的搜索
    static const std::vector<LevelDesc> levels = {
        {"入门", "只用颜色标出能走 / 不能走的点"},
        {"初级", "额外提示危险点与对手威胁"},
        {"中级", "推荐最佳着法并给出局面评估分"},
        {"高级", "给出主要变化（后续几手）与搜索深度"},
        {"大师", "给出胜率、更深的搜索与完整分析"},
    };
    return levels;
}

} // namespace chess
