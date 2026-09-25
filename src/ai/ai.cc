#include "ai/ai.h"

namespace chess {

int defaultTimeBudgetMs(int level)
{
    // 等级越高给越多时间。上限刻意压在 3s 以内：
    // 这是个交互式棋盘工具，等太久体验比棋力更重要；
    // 需要更强时可以在菜单里单独调（后续可加）。
    switch (level)
    {
    case 1: return 30;
    case 2: return 120;
    case 3: return 400;
    case 4: return 1200;
    case 5: return 3000;
    default: return 400;
    }
}

const char* levelShortName(int level)
{
    switch (level)
    {
    case 1: return "L1";
    case 2: return "L2";
    case 3: return "L3";
    case 4: return "L4";
    case 5: return "L5";
    default: return "L?";
    }
}

} // namespace chess
