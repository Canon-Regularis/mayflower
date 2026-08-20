// 128-bit board bitset. Holds up to 128 cells; the standard instance uses 100.
//
// Stored as two uint64_t, which gives explicit control over shifts across the
// 64-bit boundary. AND/OR/XOR/NOT and popcount compile the same either way.
// Re-measured in the M6 ladder.
// __uint128_t is available on this toolchain and exposed via asU128().
#pragma once

#include <bit>
#include <cstdint>

namespace mayflower {

struct Board128 {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;

    constexpr Board128() = default;
    constexpr Board128(std::uint64_t low, std::uint64_t high) : lo(low), hi(high) {}

    static constexpr Board128 bit(int index) {
        return index < 64 ? Board128{std::uint64_t{1} << index, 0}
                          : Board128{0, std::uint64_t{1} << (index - 64)};
    }

    [[nodiscard]] constexpr bool test(int index) const {
        return index < 64 ? ((lo >> index) & 1u) != 0
                          : ((hi >> (index - 64)) & 1u) != 0;
    }

    constexpr void set(int index) {
        if (index < 64) lo |= std::uint64_t{1} << index;
        else            hi |= std::uint64_t{1} << (index - 64);
    }

    constexpr void clear(int index) {
        if (index < 64) lo &= ~(std::uint64_t{1} << index);
        else            hi &= ~(std::uint64_t{1} << (index - 64));
    }

    [[nodiscard]] constexpr bool empty() const { return (lo | hi) == 0; }
    [[nodiscard]] constexpr int  count() const {
        return std::popcount(lo) + std::popcount(hi);
    }

    // Undefined if empty(); callers must guard.
    [[nodiscard]] constexpr int lowestBit() const {
        return lo != 0 ? std::countr_zero(lo) : 64 + std::countr_zero(hi);
    }

    // Iteration idiom:
    //   while (!b.empty()) { int c = b.lowestBit(); b.clearLowest(); ... }
    constexpr void clearLowest() {
        if (lo != 0) lo &= lo - 1;
        else         hi &= hi - 1;
    }

    friend constexpr Board128 operator&(Board128 a, Board128 b) { return {a.lo & b.lo, a.hi & b.hi}; }
    friend constexpr Board128 operator|(Board128 a, Board128 b) { return {a.lo | b.lo, a.hi | b.hi}; }
    friend constexpr Board128 operator^(Board128 a, Board128 b) { return {a.lo ^ b.lo, a.hi ^ b.hi}; }
    constexpr Board128 operator~() const { return {~lo, ~hi}; }

    constexpr Board128& operator&=(Board128 o) { lo &= o.lo; hi &= o.hi; return *this; }
    constexpr Board128& operator|=(Board128 o) { lo |= o.lo; hi |= o.hi; return *this; }
    constexpr Board128& operator^=(Board128 o) { lo ^= o.lo; hi ^= o.hi; return *this; }

    friend constexpr bool operator==(Board128 a, Board128 b) { return a.lo == b.lo && a.hi == b.hi; }

    [[nodiscard]] constexpr __uint128_t asU128() const {
        return (static_cast<__uint128_t>(hi) << 64) | lo;
    }
    static constexpr Board128 fromU128(__uint128_t v) {
        return {static_cast<std::uint64_t>(v), static_cast<std::uint64_t>(v >> 64)};
    }

    [[nodiscard]] constexpr bool disjointFrom(Board128 o) const {
        return ((lo & o.lo) | (hi & o.hi)) == 0;
    }
};

static_assert(sizeof(Board128) == 16, "Board128 must stay 16 bytes");

}  // namespace mayflower
