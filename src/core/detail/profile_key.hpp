// The boundary key's field widths, stated once.
//
// Five sweeps carry a boundary profile and each packs it differently. Two
// shapes are in use:
//
//   struct  profile_dp.cpp and weighted.cpp hold ext in a uint64 and vrem plus
//           the fleet index in a separate uint32, because V0 predates the
//           packed key and weighted.cpp follows it so the two stay comparable.
//   packed  profile_dp_fast.cpp, profile_dp_blocked.cpp and notouch.cpp hold
//           the whole state in one uint64, which is what lets their maps
//           compare a key in a single instruction.
//
// The widths are the shared fact: three bits per row for the horizontal
// residual, three for vrem, and ceil(log2(states)) for the fleet index. Where
// the fields sit is the rung's business and stays in the rung. notouch.cpp is
// the clearest case: it inserts a column occupancy word and a carry bit between
// ext and vrem, so its vrem shift is not 3 * height, and its masks are guarded
// against a shift of 64 that the others cannot reach.
//
// ext sits at bit 0 in every one of the five, so its accessors need no shift
// argument. vrem does not, so its accessors take one.
#pragma once

#include <bit>
#include <cstdint>

namespace mayflower::detail {

// A horizontal residual is at most maxLen - 1, and maxLen is capped at 8, so
// three bits hold it and 20 rows need 60. The cap is what makes the packed key
// fit alongside vrem, the fleet index and the fast rung's epoch.
inline constexpr int kExtBits = 3;
inline constexpr std::uint64_t kExtMask = (std::uint64_t{1} << kExtBits) - 1;

// vrem counts rows a vertical ship still occupies, so it shares the bound.
inline constexpr int kVremBits = 3;
inline constexpr std::uint64_t kVremMask = (std::uint64_t{1} << kVremBits) - 1;

[[nodiscard]] constexpr int extDigit(std::uint64_t word, int row) {
    return static_cast<int>((word >> (kExtBits * row)) & kExtMask);
}

[[nodiscard]] constexpr std::uint64_t setExtDigit(std::uint64_t word, int row, int value) {
    const int shift = kExtBits * row;
    return (word & ~(kExtMask << shift)) | (static_cast<std::uint64_t>(value) << shift);
}

[[nodiscard]] constexpr int vremAt(std::uint64_t word, int shift) {
    return static_cast<int>((word >> shift) & kVremMask);
}

[[nodiscard]] constexpr std::uint64_t setVremAt(std::uint64_t word, int shift, int value) {
    return (word & ~(kVremMask << shift)) | (static_cast<std::uint64_t>(value) << shift);
}

// Bits needed to index `stateCount` fleet-usage states. Written two ways before
// this: a countl_zero expression in notouch.cpp and profile_dp_blocked.cpp, and
// a shift-and-count loop twice in profile_dp_fast.cpp, once in the support
// predicate and once in the driver. The two agree on every input, and a sweep
// whose support predicate and driver disagreed on the width would accept an
// instance it then packs wrongly.
[[nodiscard]] constexpr int fleetIndexBits(int stateCount) {
    if (stateCount <= 1) return 0;
    return 64 - std::countl_zero(static_cast<std::uint64_t>(stateCount - 1));
}

// The struct shape. aux carries vrem in its low three bits and the fleet index
// above, so it packs the same two fields the packed shape puts above ext.
struct Key {
    std::uint64_t ext = 0;
    std::uint32_t aux = 0;

    friend bool operator==(const Key& a, const Key& b) {
        return a.ext == b.ext && a.aux == b.aux;
    }
};

[[nodiscard]] constexpr std::uint32_t packAux(int vrem, int fleetIdx) {
    return static_cast<std::uint32_t>(vrem) |
           (static_cast<std::uint32_t>(fleetIdx) << kVremBits);
}
[[nodiscard]] constexpr int auxVrem(std::uint32_t aux) {
    return static_cast<int>(aux & static_cast<std::uint32_t>(kVremMask));
}
[[nodiscard]] constexpr int auxFleet(std::uint32_t aux) {
    return static_cast<int>(aux >> kVremBits);
}

}  // namespace mayflower::detail
