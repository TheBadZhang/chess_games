#include "ai/gf2.h"

#include <algorithm>

namespace chess {

uint64_t Gf2System::bitMask(int var) const
{
    return 1ull << static_cast<unsigned>(var & 63);
}

void Gf2System::init(int nVars, int nEqs)
{
    nVars_ = nVars;
    nEqs_  = nEqs;
    // 多留一位给 rhs
    words_ = (nVars + 1 + 63) / 64;
    if (words_ < 1)
    {
        words_ = 1;
    }
    rows_.assign(static_cast<size_t>(nEqs_) * words_, 0ull);
    nullBasis_.clear();
}

bool Gf2System::get(int eq, int var) const
{
    const uint64_t word = rows_[static_cast<size_t>(eq) * words_ + (var >> 6)];
    return (word & bitMask(var)) != 0;
}

bool Gf2System::getRhs(int eq) const
{
    // rhs 位于位下标 nVars_
    const int      var  = nVars_;
    const uint64_t word = rows_[static_cast<size_t>(eq) * words_ + (var >> 6)];
    return (word & bitMask(var)) != 0;
}

void Gf2System::set(int eq, int var, bool value)
{
    uint64_t& word = rows_[static_cast<size_t>(eq) * words_ + (var >> 6)];
    if (value)
    {
        word |= bitMask(var);
    }
    else
    {
        word &= ~bitMask(var);
    }
}

void Gf2System::setRhs(int eq, bool value)
{
    set(eq, nVars_, value);
}

void Gf2System::xorRow(int dst, int src)
{
    uint64_t*       d = &rows_[static_cast<size_t>(dst) * words_];
    const uint64_t* s = &rows_[static_cast<size_t>(src) * words_];
    for (int w = 0; w < words_; ++w)
    {
        d[w] ^= s[w];
    }
}

void Gf2System::swapRow(int a, int b)
{
    if (a == b)
    {
        return;
    }
    uint64_t* x = &rows_[static_cast<size_t>(a) * words_];
    uint64_t* y = &rows_[static_cast<size_t>(b) * words_];
    for (int w = 0; w < words_; ++w)
    {
        std::swap(x[w], y[w]);
    }
}

bool Gf2System::rowIsZero(int eq) const
{
    for (int var = 0; var < nVars_; ++var)
    {
        if (get(eq, var))
        {
            return false;
        }
    }
    return true;
}

bool Gf2System::solve(std::vector<uint8_t>& particular, int* nullity)
{
    nullBasis_.clear();

    // ---- 高斯消元（化为行最简形）----
    // 就地修改 rows_：求解后本对象不再保证是原始矩阵，这对本用途无妨
    // （调用方每次都会重新 init）。
    std::vector<int> pivotRowOfCol(nVars_, -1);

    int row = 0;
    for (int col = 0; col < nVars_ && row < nEqs_; ++col)
    {
        int sel = -1;
        for (int r = row; r < nEqs_; ++r)
        {
            if (get(r, col))
            {
                sel = r;
                break;
            }
        }
        if (sel < 0)
        {
            continue;   // 该列无主元 -> 自由变量
        }

        swapRow(row, sel);

        // 消去该列的其他所有行的这一位（含上三角部分 -> 直接得到行最简形）
        for (int r = 0; r < nEqs_; ++r)
        {
            if (r != row && get(r, col))
            {
                xorRow(r, row);
            }
        }

        pivotRowOfCol[col] = row;
        ++row;
    }

    // ---- 一致性检查：0 = 1 的方程意味着无解 ----
    for (int r = row; r < nEqs_; ++r)
    {
        if (rowIsZero(r) && getRhs(r))
        {
            return false;
        }
    }

    // ---- 特解：自由变量取 0，主元变量取对应 rhs ----
    particular.assign(nVars_, 0);
    for (int col = 0; col < nVars_; ++col)
    {
        const int r = pivotRowOfCol[col];
        if (r >= 0)
        {
            particular[col] = getRhs(r) ? 1 : 0;
        }
    }

    // ---- 零空间基：逐个自由变量置 1，回代求主元变量 ----
    for (int freeVar = 0; freeVar < nVars_; ++freeVar)
    {
        if (pivotRowOfCol[freeVar] >= 0)
        {
            continue;
        }

        std::vector<uint8_t> basis(nVars_, 0);
        basis[freeVar] = 1;

        for (int col = 0; col < nVars_; ++col)
        {
            const int r = pivotRowOfCol[col];
            if (r < 0)
            {
                continue;
            }
            // 该行形如 x_col + sum(自由变量) = rhs，齐次情形 rhs 视为 0
            if (get(r, freeVar))
            {
                basis[col] = 1;
            }
        }
        nullBasis_.push_back(std::move(basis));
    }

    if (nullity)
    {
        *nullity = static_cast<int>(nullBasis_.size());
    }
    return true;
}

} // namespace chess
