// The two hashes the sweeps use, named.
//
// Four sweeps carried a mix() that was character identical: profile_dp.cpp,
// weighted.cpp, notouch.cpp and profile_dp_blocked.cpp. profile_dp_fast.cpp
// carried a fifth under the same name that is a different function: no additive
// step, and shifts of 31 and 29 rather than 30, 27 and 31.
//
// That difference is deliberate. The fast rung probes its map on every emitted
// edge, so it uses a cheaper mixer and accepts slightly worse spreading. Giving
// the two functions separate names records that choice.
//
// Do not substitute one for the other without measuring. Both feed open
// addressed tables, so the mixer determines the probe sequence. A worse mixer
// produces a slower sweep, not a wrong answer, so the test suite cannot detect
// the regression.
#pragma once

#include <cstdint>

namespace mayflower::detail {

// splitmix64's finaliser, with the additive step. The general purpose choice.
[[nodiscard]] inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Two multiplies and two shifts. Used by the packed key rung, where the mixer
// sits on the hot path and the keys are already well spread by construction.
[[nodiscard]] inline std::uint64_t fastMix(std::uint64_t x) {
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 31;
    x *= 0x94D049BB133111EBull;
    return x ^ (x >> 29);
}

}  // namespace mayflower::detail
