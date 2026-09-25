#pragma once

#include "games/game.h"

#include <vector>

namespace chess {

// 所有已注册的游戏（顺序即菜单顺序）
const std::vector<GameDesc>& allGames();

// 工具类场景（不是游戏，但有独立画面）——菜单里单独一组
struct ToolDesc
{
    const char* id    = "";
    const char* name  = "";
    const char* blurb = "";
};

const std::vector<ToolDesc>& allTools();

// 按 id 查游戏描述符；找不到返回 nullptr
const GameDesc* findGame(const char* id);

// 5 个等级的默认描述。游戏的 GameDesc::levels 为空时用它兜底，
// 保证菜单与 HUD 在任何游戏下都有一致的等级说明。
const std::vector<LevelDesc>& defaultLevels();

} // namespace chess
