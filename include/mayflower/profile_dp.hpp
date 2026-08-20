// Exact model counting over fleet configurations by a broken-profile
// transfer-matrix DP. Counts all 15,046,987,768 configurations of the standard
// instance in one sweep over a lattice of 2.87e7 edges, without enumerating a
// board.
//
// Cells are scanned in column-major order carrying a boundary profile.
//
// STATE
//   ext[row] in 0..maxLen-1   columns a horizontal ship in `row` still extends
//                             into (3 bits per row, packed into a uint64)
//   vrem     in 0..maxLen-1   rows a vertical ship in the current column still
//                             occupies
//   fleetUsed                 mixed-radix index over how many of each distinct
//                             length have been STARTED
//
// The fleet counter is decremented at ship START, so it records how many ships
// of each length are in play and never which. Ship identity therefore stays out
// of the profile, and indistinguishable ships (the two 3-ships) are counted
// correctly with no division by 2!.
//
// TRANSITIONS at cell (row, col), d = ext[row]:
//   d > 0                  horizontal continuation; illegal if vrem > 0
//   d == 0, vrem > 0       vertical continuation
//   d == 0, vrem == 0      leave empty, or start a horizontal ship
//                          (col+L <= width) or a vertical ship (row+L <= height)
//
// Observations are per-cell filters, so they shrink the live state set: on the
// standard instance 10 misses cut it to 12.7% of the prior, 30 misses to 0.4%.
//
// M1 remaining work: the order-aware SUNK(x,L) predicate. SUNK means the shot at
// x sank the ship, so every other cell of that ship was already shot before that
// point; requiring only cells(ship) subset-of HIT over-counts. This header
// exposes per-cell constraints only, so no caller can obtain wrong sunk
// semantics in the meantime. See docs/ORDER_DEPENDENCE.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mayflower/instance.hpp"

namespace mayflower {

struct CountResult {
    std::uint64_t count = 0;       // |Omega| under the given constraints
    std::size_t   peakStates = 0;  // largest live layer
    std::uint64_t stateVisits = 0; // states processed across all cells
    std::uint64_t edges = 0;       // transitions relaxed
};

// `cells` is row-major with size == instance.cellCount(). Pass all-Free for the
// unconstrained prior count.
CountResult countConfigurations(const Instance& inst,
                                const std::vector<CellConstraint>& cells);

CountResult countConfigurations(const Instance& inst);

// Exact occupancy marginal for one cell, as a constrained count.
//
// Cost is O(cells) full counts for a whole heatmap, or 15 under D4 symmetry.
// Forward-backward on the same lattice gives every cell marginal in one forward
// plus one backward pass, about 2x a single count; that lands in M2 and will be
// validated against this function.
std::uint64_t occupancyCount(const Instance& inst, int row, int col);

}  // namespace mayflower
