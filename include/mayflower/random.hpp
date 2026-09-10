// One home for the pseudo-random streams.
//
// splitmix64's three constants were retyped across fourteen files: eleven
// complete finalisers in C++, four copies of the same ten line Rng struct, and
// five copies of the 53 bit conversion to a double. Two of those copies already
// carried comments admitting the duplication rather than removing it.
//
// Three distinct things live here, and the distinction matters more than the
// sharing does:
//
//   mix64        the finaliser alone. Keyed derivation uses this, because it
//                takes an index plus a key rather than advancing a counter.
//   splitmix64   the finaliser with the additive step folded in. This is the
//                value a counter sitting at x produces.
//   Rng          a counter-based stream over splitmix64.
//
// Rng offers two draws below a bound and they are not interchangeable. `below`
// takes the remainder, which is very slightly biased towards the low end and is
// the right cost for choosing a cell in a policy or a test. `belowUnbiased`
// rejects the ragged tail instead, and is what the board generator uses, where
// a bias would poison every statistic computed on top of it.
//
// Two hashes elsewhere are deliberately NOT this one and must stay separate:
// detail::fastMix in src/core/detail/hashing.hpp is a cheaper mixer for the
// packed key rung's probe, and foldHash in folds.hpp is a salted variant whose
// output vector is pinned digit for digit against python/stats.py.
#pragma once

#include <cstdint>
#include <stdexcept>

namespace mayflower {

// The additive step. Named because it is also the stride of the stream.
inline constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ull;

// 2^53, where a double stops representing consecutive integers. The divisor
// that turns 53 random bits into a uniform double.
inline constexpr double kTwoPow53 = 9007199254740992.0;

// The policy stream's key. Common random numbers require a policy's draws to be
// keyed apart from the board pool: deriving one from the other gives each board
// its own policy randomisation, which measures a family of policies rather than
// one and can score below the true optimum. The exact solver's domination
// invariant caught that once already.
inline constexpr std::uint64_t kPolicyStreamKey = 0xD1B54A32D192ED03ull;

// The finaliser, without the additive step.
[[nodiscard]] inline std::uint64_t mix64(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// The value of a counter sitting at x.
[[nodiscard]] inline std::uint64_t splitmix64(std::uint64_t x) {
    return mix64(x + kGoldenGamma);
}

// A seed derived from an index and a key, independent of any counter. Used
// where each item needs its own stream and the streams must not be correlated.
[[nodiscard]] inline std::uint64_t keyedSeed(std::uint64_t index, std::uint64_t key) {
    return mix64(index + key);
}

// A counter-based stream. Seeded at s, so a stream built anywhere from the same
// seed produces the same sequence.
struct Rng {
    std::uint64_t s = 0;

    Rng() = default;
    explicit Rng(std::uint64_t seed) : s(seed) {}

    std::uint64_t next() {
        const std::uint64_t r = splitmix64(s);
        s += kGoldenGamma;
        return r;
    }

    // Refused rather than divided by. Every policy ends in below(free.size()),
    // and a policy asked to choose with nothing free reached a remainder by
    // zero. The failure was a crash rather than an error.
    int below(int n) {
        if (n <= 0) throw std::invalid_argument("below() needs a positive bound");
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }

    // Unbiased over [0, n) by rejecting the ragged tail. Slower, and the right
    // choice wherever the uniformity is the result rather than a convenience.
    std::uint64_t belowUnbiased(std::uint64_t n) {
        if (n == 0) throw std::invalid_argument("belowUnbiased() needs a positive bound");
        const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % n) - 1;
        std::uint64_t r;
        do { r = next(); } while (r > limit);
        return r % n;
    }

    // The top 53 bits as a double in [0, 1).
    double unit() { return static_cast<double>(next() >> 11) / kTwoPow53; }
};

}  // namespace mayflower
