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

// Placements of a length-L ship: H*(W-L+1) horizontal + W*(H-L+1) vertical,
// the vertical term dropped at L = 1 where the two orientations coincide.
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

inline constexpr double kPriorEntropyBits = 33.80875565399003;   // log2(kOmega0)
// Full precision, not the 33.8088 the reports display: the entropy rung below
// divides by it, and a 4-decimal value moved that rung in the fifth.

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
// Derived rather than typed. It was pinned at the rounded 13.08 while
// tools/bounds computed and printed 13.0790 for the same rung, so the one
// quantity had two written forms that could drift apart.
inline constexpr double kEntropyBound        = kPriorEntropyBits / kMaxBitsPerShot;

// ---------------------------------------------------------------------------
// Statistics.
// ---------------------------------------------------------------------------

// The two-sided normal quantile at 95 percent, to full double precision, which
// is the same standard kMaxBitsPerShot above is written to.
//
// It had seven typed sites in two spellings. tools/report_style.py declared
// 1.959963985 and its docstring said the retyping had been fixed in three
// places; it had been fixed in the three Python renderer places, and
// tools/selfplay.cpp, tools/report_data.cpp and python/stats.py went on writing
// the shorter 1.959964. The two reached the same page: report_data computed a
// policy's interval with one and tools/collect_results.py computed further
// intervals over that same data with the other.
//
// Neither spelling was right. The value is 1.959963984540054, so the long form
// was out by 4.6e-10 and the short by 1.5e-8. Both are far below anything the
// report prints, which is why this drifted for so long without showing.
//
// python/stats.py and tools/report_style.py carry their own copy, because the
// analysis and report layers cannot include a C++ header. tests/test_folds.cpp
// pins the fold vector across that same boundary and test_stated_counts pins
// these three against each other for the same reason.
inline constexpr double kZ95 = 1.959963984540054;

// ---------------------------------------------------------------------------
// Symmetry.
// ---------------------------------------------------------------------------

// D4 partitions the 100 cells into 15 orbits (representatives 0 <= i <= j <= 4),
// a 6.67x saving on any per-cell computation.
inline constexpr int kD4OrbitCount = 15;

// ---------------------------------------------------------------------------

// Placements of a length-L ship on a w by h board, ignoring other ships.
//
// A length-1 ship has one orientation, not two: counting the vertical branch as
// well returns twice the truth, which is the double-count four of the sweeps
// carried. The two dimension guards matter for the same reason the length one
// does, and this had them on only one of its two copies: Instance::placementsFor
// checked width >= L and height >= L and the consteval twin here did not, so a
// ship longer than the board gave a negative term rather than zero. Unreachable
// from the static_asserts below, which pass 10x10 and L <= 5, and one formula
// with two spellings all the same.
//
// constexpr rather than consteval, because Instance::placementsFor is the
// runtime caller and now defers to this.
[[nodiscard]] constexpr int placementsFor(int L, int w, int h) {
    int n = 0;
    if (w >= L) n += h * (w - L + 1);
    if (L > 1 && h >= L) n += w * (h - L + 1);
    return n;
}

namespace detail {
consteval int shipCellSum() {
    int s = 0;
    for (int L : kFleet) s += L;
    return s;
}
}  // namespace detail

static_assert(placementsFor(5, kBoardWidth, kBoardHeight) == kPlacements5);
static_assert(placementsFor(4, kBoardWidth, kBoardHeight) == kPlacements4);
static_assert(placementsFor(3, kBoardWidth, kBoardHeight) == kPlacements3);
static_assert(placementsFor(2, kBoardWidth, kBoardHeight) == kPlacements2);
static_assert(detail::shipCellSum() == kShipCells, "fleet cell count must be 17");
static_assert(kCellCount <= 128, "the board must stay inside the 128-cell bound");
static_assert(kMaxAccumulator < (std::uint64_t{1} << 38), "accumulator headroom check");

}  // namespace mayflower::constants
