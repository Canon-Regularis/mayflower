// Project constants. Every module imports from here; nothing hardcodes them.
// VERIFIED values were computed by two independent implementations and
// cross-checked against brute-force enumeration on small instances.
#pragma once

#include <cstdint>

namespace mayflower::constants {

// ---------------------------------------------------------------------------
// Standard instance: 10x10, fleet {5,4,3,3,2}, ships may touch.
// ---------------------------------------------------------------------------

inline constexpr int kBoardWidth  = 10;
inline constexpr int kBoardHeight = 10;
inline constexpr int kCellCount   = kBoardWidth * kBoardHeight;   // 100

// The two 3-ships are indistinguishable.
inline constexpr int kFleetSize  = 5;
inline constexpr int kShipCells  = 17;   // coverage lower bound on shots
inline constexpr int kFleet[kFleetSize] = {5, 4, 3, 3, 2};

// Placements of a length-L ship: H*(W-L+1) horizontal + W*(H-L+1) vertical.
// On 10x10 that is 20*(11-L).
inline constexpr int kPlacements5 = 120;
inline constexpr int kPlacements4 = 140;
inline constexpr int kPlacements3 = 160;
inline constexpr int kPlacements2 = 180;
inline constexpr int kDistinctPlacements = kPlacements5 + kPlacements4 + kPlacements3 + kPlacements2;  // 600

// ---------------------------------------------------------------------------
// Hypothesis space.
// ---------------------------------------------------------------------------

// VERIFIED. Legal fleet configurations on 10x10 with ships allowed to touch,
// counting physical boards (the two 3-ships indistinguishable).
inline constexpr std::uint64_t kOmega0 = 15'046'987'768ull;

// The same count with the 3-ships labelled. Published figures usually quote this.
inline constexpr std::uint64_t kOmega0Labelled = 2 * kOmega0;   // 30,093,975,536

// The same board and fleet under the printed-puzzle rule, where distinct ships
// may not share an edge or a corner. Reproduced by src/core/notouch.cpp and by
// literal enumeration on the small-board ladder.
inline constexpr std::uint64_t kOmegaNoTouch = 1'925'751'392ull;
static_assert(kOmegaNoTouch < kOmega0, "forbidding contact cannot add configurations");

inline constexpr double kPriorEntropyBits = 33.8088;            // log2(kOmega0)

// Sum over cells of P(cell occupied) under the uniform prior, exactly 17.
inline constexpr int kMarginalSum = kShipCells;

// Largest accumulator in the unweighted count path: 17*kOmega0 = 2^37.90, so
// uint64 has ~26 bits spare. Products of two counts need u128: kOmega0^2 is
// 68 bits.
inline constexpr std::uint64_t kMaxAccumulator = kShipCells * kOmega0;   // 255,798,792,056

// ---------------------------------------------------------------------------
// Bounds ladder. Entries not yet re-derived in this repo are omitted; add them
// as their tools land.
// ---------------------------------------------------------------------------

// E1: every ship cell must be shot, so T >= 17.
inline constexpr int kCoverageBound = kShipCells;

// E2: H(Omega_0) / log2(6), where the outcome alphabet is
// {MISS, HIT, SUNK(2), SUNK(3), SUNK(4), SUNK(5)}. Evaluates to 13.08, which
// falls below E1, so coverage is the binding constraint.
inline constexpr int    kOutcomeAlphabetSize = 6;
inline constexpr double kMaxBitsPerShot      = 2.5849625007211562;  // log2(6)
inline constexpr double kEntropyBound        = 13.08;

// ---------------------------------------------------------------------------
// Symmetry.
// ---------------------------------------------------------------------------

// D4 partitions the 100 cells into 15 orbits (representatives 0 <= i <= j <= 4),
// a 6.67x saving on any per-cell computation.
inline constexpr int kD4OrbitCount = 15;

// ---------------------------------------------------------------------------

namespace detail {
consteval int placementsFor(int L, int w, int h) {
    return h * (w - L + 1) + w * (h - L + 1);
}
consteval int shipCellSum() {
    int s = 0;
    for (int L : kFleet) s += L;
    return s;
}
}  // namespace detail

static_assert(detail::placementsFor(5, kBoardWidth, kBoardHeight) == kPlacements5);
static_assert(detail::placementsFor(4, kBoardWidth, kBoardHeight) == kPlacements4);
static_assert(detail::placementsFor(3, kBoardWidth, kBoardHeight) == kPlacements3);
static_assert(detail::placementsFor(2, kBoardWidth, kBoardHeight) == kPlacements2);
static_assert(detail::shipCellSum() == kShipCells, "fleet cell count must be 17");
static_assert(kCellCount <= 128, "Board128 holds at most 128 cells");
static_assert(kMaxAccumulator < (std::uint64_t{1} << 38), "accumulator headroom check");

}  // namespace mayflower::constants
