#pragma once

#include <cstdint>
#include <vector>

namespace chess {

// ---------------------------------------------------------------------------
//  GF(2) 上的线性方程组求解
// ---------------------------------------------------------------------------
//  用于"翻转棋"（Lights-Out 类规则）：每个格子的按下与否是一个 0/1 变量，
//  每个格子的颜色要求是一个 0/1 方程，整组约束在 GF(2) 上线性。
//  于是可以精确求解：得到方案、最少步数、以及"哪些格子所有解都必须按"。
//
//  用位压缩 + 高斯消元实现，规模上限只受内存限制
//  （20x20=400 变量时约 400 行 x 7 个 uint64，仍然很快）。
//
//  位布局：每行是 (nVars + 1) 位，位下标 [0, nVars) 是系数，
//  位下标 nVars 是右端项（rhs）。
// ---------------------------------------------------------------------------
class Gf2System
{
public:
    void init(int nVars, int nEqs);

    int nVars() const { return nVars_; }
    int nEqs() const { return nEqs_; }

    void set(int eq, int var, bool value);
    void setRhs(int eq, bool value);

    // 求出一个特解（自由变量取 0，主元变量取 rhs）。
    // 无解返回 false。nullity 非空时写入自由变量个数。
    bool solve(std::vector<uint8_t>& particular, int* nullity);

    // 零空间的一组基（每个向量长度 nVars()）。
    // 全部解 = 特解 + span(basis)。
    const std::vector<std::vector<uint8_t>>& nullBasis() const { return nullBasis_; }

private:
    bool     get(int eq, int var) const;
    bool     getRhs(int eq) const;
    bool     rowIsZero(int eq) const;
    void     xorRow(int dst, int src);
    void     swapRow(int a, int b);
    uint64_t bitMask(int var) const;

    int nVars_ = 0;
    int nEqs_  = 0;
    int words_ = 0;   // 每行的 uint64 数 = ceil((nVars + 1) / 64)

    std::vector<uint64_t> rows_;
    std::vector<std::vector<uint8_t>> nullBasis_;
};

} // namespace chess
