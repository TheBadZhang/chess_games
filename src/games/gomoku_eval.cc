#include "games/gomoku_eval.h"

#include <algorithm>
#include <cstring>

namespace chess {
namespace gomoku {

// ---------------------------------------------------------------------------
//  棋盘
// ---------------------------------------------------------------------------

void Board::reset(int c, int r)
{
    cols = c;
    rows = r;
    cells.assign(static_cast<size_t>(c) * r, 0);
}

// ---------------------------------------------------------------------------
//  棋型判定
// ---------------------------------------------------------------------------
//  做法：把 (x,y) 所在的线取一段长度为 9 的窗口（中心就是假设要落的子），
//  转成字符序列：
//      '1' = 自己   '2' = 对方或棋盘外（都算堵）   '0' = 空
//  然后按"从高分到低分"匹配子串。顺序很关键，例如 "011110"（活四）
//  同时包含 "01111" 与 "11110"，必须先判活四。
// ---------------------------------------------------------------------------

namespace {

// 窗口长度固定为 9（中心 +/- 4）
constexpr int kWindow = 9;

// 把 (x,y) 所在线取一朦 9 格窗口转成字符序列，(x,y) 处强制为 '1'。
//
// 刻意写入调用方提供的定长缓冲，而不是返回 std::string：
// evaluate() 每个叶节点要调上百次棋型判定，用 std::string 会带来
// 上千次堆分配，实测单节点成本高达 1.7ms，从而让搜索**严重超出时间预算**。
//
// 返回值是窗口里"自己棋子"的个数，供 shapeAt() 做早退判断。
int lineWindowInto(const Board& b, int x, int y, int side, int dx, int dy, char* out)
{
    int own = 1;   // 中心的那一枚

    for (int k = -4; k <= 4; ++k)
    {
        if (k == 0)
        {
            out[k + 4] = '1';
            continue;
        }
        const uint8_t v = b.at(x + k * dx, y + k * dy);
        if (v == 0)
        {
            out[k + 4] = '0';
        }
        else if (v == side)
        {
            out[k + 4] = '1';
            ++own;
        }
        else
        {
            out[k + 4] = '2';
        }
    }
    out[kWindow] = '\0';
    return own;
}

// 在长度为 kWindow 的缓冲里找子串；plen 由调用方用 sizeof 算好，
// 避免在热点路径上反复 strlen。
inline bool hasPatN(const char* s, const char* pat, int plen)
{
    for (int i = 0; i + plen <= kWindow; ++i)
    {
        int j = 0;
        while (j < plen && s[i + j] == pat[j])
        {
            ++j;
        }
        if (j == plen)
        {
            return true;
        }
    }
    return false;
}

// 用字面量长度代替 strlen
#define HAS_PAT(s, lit) hasPatN((s), (lit), static_cast<int>(sizeof(lit) - 1))

} // namespace

int runLength(const Board& b, int x, int y, int side, int dir)
{
    const int dx = kDirs[dir][0];
    const int dy = kDirs[dir][1];

    int n = 1;

    for (int s = 1; s <= 5; ++s)
    {
        if (b.at(x + s * dx, y + s * dy) == side)
        {
            ++n;
        }
        else
        {
            break;
        }
    }
    for (int s = 1; s <= 5; ++s)
    {
        if (b.at(x - s * dx, y - s * dy) == side)
        {
            ++n;
        }
        else
        {
            break;
        }
    }
    return n;
}

Shape shapeAt(const Board& b, int x, int y, int side, int dir)
{
    // 长连优先：>= 6 在后面匹配里也会命中 "11111"，必须先单独判掉
    if (runLength(b, x, y, side, dir) >= 6)
    {
        return Shape::Overline;
    }

    char s[kWindow + 1];
    const int own = lineWindowInto(b, x, y, side, kDirs[dir][0], kDirs[dir][1], s);

    // 早退：窗口里只有中心这一枚己方子。
    // 任何 2 子及以上的棋型都不可能匹配，直接给 One。
    // 棋盘空的时候绝大多数判定都落在这一支上，这一条能省掉下面全部模式匹配。
    if (own <= 1)
    {
        return Shape::One;
    }

    if (HAS_PAT(s, "11111"))
    {
        return Shape::Five;
    }
    if (HAS_PAT(s, "011110"))
    {
        return Shape::OpenFour;
    }
    // 冲四：只差一个点成五。10111/11011/11101 是中间有断点的冲四
    if (HAS_PAT(s, "01111") || HAS_PAT(s, "11110") || HAS_PAT(s, "10111") ||
        HAS_PAT(s, "11011") || HAS_PAT(s, "11101"))
    {
        return Shape::Four;
    }
    // 活三：下一步能变成活四（两头都能延伸）
    if (HAS_PAT(s, "011100") || HAS_PAT(s, "001110") || HAS_PAT(s, "011010") ||
        HAS_PAT(s, "010110"))
    {
        return Shape::OpenThree;
    }
    // 眠三：只能变成冲四
    if (HAS_PAT(s, "001112") || HAS_PAT(s, "211100") || HAS_PAT(s, "11100") ||
        HAS_PAT(s, "00111") || HAS_PAT(s, "010112") || HAS_PAT(s, "211010") ||
        HAS_PAT(s, "011012") || HAS_PAT(s, "210110") || HAS_PAT(s, "11001") ||
        HAS_PAT(s, "10011") || HAS_PAT(s, "10101"))
    {
        return Shape::Three;
    }
    // 活二
    if (HAS_PAT(s, "001100") || HAS_PAT(s, "011000") || HAS_PAT(s, "000110") ||
        HAS_PAT(s, "010100") || HAS_PAT(s, "001010") || HAS_PAT(s, "010010"))
    {
        return Shape::OpenTwo;
    }
    // 眠二
    if (HAS_PAT(s, "000100") || HAS_PAT(s, "001000") || HAS_PAT(s, "010000") ||
        HAS_PAT(s, "000010"))
    {
        return Shape::Two;
    }
    if (HAS_PAT(s, "010") || HAS_PAT(s, "100") || HAS_PAT(s, "001"))
    {
        return Shape::One;
    }
    return Shape::None;
}

Shape bestShape(const Board& b, int x, int y, int side)
{
    Shape best = Shape::None;
    for (int d = 0; d < 4; ++d)
    {
        const Shape s = shapeAt(b, x, y, side, d);
        if (static_cast<int>(s) > static_cast<int>(best))
        {
            best = s;
        }
    }
    return best;
}

int shapeScore(Shape s)
{
    // 量级刻意拉开：一个"活四"必须比两个"活三"更值钱（活四是必胜，双活三不一定）
    switch (s)
    {
    case Shape::Five:      return 10000000;
    case Shape::Overline:  return 10000000;   // 自由规则下长连也算胜
    case Shape::OpenFour:  return 1000000;
    case Shape::Four:      return 100000;
    case Shape::OpenThree: return 50000;
    case Shape::Three:     return 5000;
    case Shape::OpenTwo:   return 500;
    case Shape::Two:       return 100;
    case Shape::One:       return 10;
    default:               return 0;
    }
}

int pointScore(const Board& b, int x, int y, int side)
{
    int total = 0;
    for (int d = 0; d < 4; ++d)
    {
        const Shape s = shapeAt(b, x, y, side, d);
        if (s == Shape::Five || s == Shape::Overline)
        {
            return shapeScore(Shape::Five);   // 成五直接返回，不必再算别的方向
        }
        total += shapeScore(s);
    }
    return total;
}

bool makesFiveOrMore(const Board& b, int x, int y, int side)
{
    for (int d = 0; d < 4; ++d)
    {
        if (runLength(b, x, y, side, d) >= 5)
        {
            return true;
        }
    }
    return false;
}

bool makesExactlyFive(const Board& b, int x, int y, int side)
{
    for (int d = 0; d < 4; ++d)
    {
        if (runLength(b, x, y, side, d) == 5)
        {
            return true;
        }
    }
    return false;
}

bool isWinAt(const Board& b, int x, int y, int side)
{
    // 自由规则：五连及以上都算胜
    return makesFiveOrMore(b, x, y, side);
}

// ---------------------------------------------------------------------------
//  禁手
// ---------------------------------------------------------------------------

ForbiddenInfo checkForbidden(const Board& b, int x, int y)
{
    ForbiddenInfo info;

    // 成五优先于一切禁手。
    // 这里必须用 makesExactlyFive 而不是 makesFiveOrMore —— 后者把长连也算进去了，
    // 会让长连被当成"成五"放行，长连禁手就永远检测不到。
    if (makesExactlyFive(b, x, y, 1))
    {
        return info;
    }

    for (int d = 0; d < 4; ++d)
    {
        const Shape s = shapeAt(b, x, y, 1, d);
        switch (s)
        {
        case Shape::Overline:
            info.overline = true;
            break;
        case Shape::OpenFour:
        case Shape::Four:
            ++info.fourCount;
            break;
        case Shape::OpenThree:
            ++info.openThreeCount;
            break;
        default:
            break;
        }
    }

    info.forbidden = info.overline || info.fourCount >= 2 || info.openThreeCount >= 2;
    return info;
}

const char* shapeName(Shape s)
{
    switch (s)
    {
    case Shape::Five:      return "成五";
    case Shape::Overline:  return "长连";
    case Shape::OpenFour:  return "活四";
    case Shape::Four:      return "冲四";
    case Shape::OpenThree: return "活三";
    case Shape::Three:     return "眠三";
    case Shape::OpenTwo:   return "活二";
    case Shape::Two:       return "眠二";
    case Shape::One:       return "单子";
    default:               return "无";
    }
}

} // namespace gomoku
} // namespace chess
