#pragma once

#include <cstdint>

namespace chess {

// ---------------------------------------------------------------------------
//  确定性随机数发生器（xoshiro256** + splitmix64 播种）
// ---------------------------------------------------------------------------
//  为什么不用 rand()：
//    MCTS / 随机开局需要"同一种子 -> 同一结果"，否则回归测试无法稳定断言。
//    rand() 的实现随 CRT 而异，且全局状态会被其他代码干扰。
// ---------------------------------------------------------------------------
class Rng
{
public:
    Rng() { seed(0x9E3779B97F4A7C15ull); }
    explicit Rng(uint64_t s) { seed(s); }

    void seed(uint64_t s)
    {
        uint64_t x = s;
        for (int i = 0; i < 4; ++i)
        {
            s_[i] = splitmix(&x);
        }
    }

    uint64_t nextU64()
    {
        const uint64_t result = rotl(s_[1] * 5u, 7) * 9u;
        const uint64_t t      = s_[1] << 17;

        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);

        return result;
    }

    // [0, n) —— Lemire 乘法取模（游戏场景下可接受的微弱偏差，且无除法）
    uint32_t below(uint32_t n)
    {
        if (n == 0)
        {
            return 0;
        }
        return static_cast<uint32_t>(((nextU64() >> 32) * static_cast<uint64_t>(n)) >> 32);
    }

    // [lo, hi] 闭区间
    int nextInt(int lo, int hi)
    {
        if (hi <= lo)
        {
            return lo;
        }
        return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1)));
    }

    double nextDouble()
    {
        return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }

    // 以概率 p 返回 true
    bool chance(double p) { return nextDouble() < p; }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    static uint64_t splitmix(uint64_t* x)
    {
        uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    uint64_t s_[4]{};
};

} // namespace chess
