// Exact model counting over fleet configurations by a broken-profile
// transfer-matrix DP. Counts all 15,046,987,768 configurations of the standard
// instance in one sweep over a lattice of 2.87e7 edges, without enumerating a
// board.
//
// Cells are scanned in column-major order carrying a boundary profile.
//
// State.
//   ext[row] in 0..maxLen-1   columns a horizontal ship in `row` still extends
//                             into (3 bits per row, packed into a uint64)
//   vrem     in 0..maxLen-1   rows a vertical ship in the current column still
//                             occupies
//   fleetUsed                 mixed-radix index over how many of each distinct
//                             length have been started
//
// The fleet counter is decremented at ship START, so it records how many ships
// of each length are in play and never which. Ship identity therefore stays out
// of the profile, and indistinguishable ships (the two 3-ships) are counted
// correctly with no division by 2!.
//
// Transitions at cell (row, col), d = ext[row]:
//   d > 0                  horizontal continuation; illegal if vrem > 0
//   d == 0, vrem > 0       vertical continuation
//   d == 0, vrem == 0      leave empty, or start a horizontal ship
//                          (col+L <= width) or a vertical ship (row+L <= height)
//
// A length-1 ship starts horizontally only. Both branches would emit the same
// single cell, and every sweep that emitted both returned 2^k times the truth
// for a fleet of k of them.
//
// Observations enter as a per-cell filter plus a per-placement gate consulted on
// START transitions, so they shrink the live state set: on the standard instance
// 10 misses cut it to 12.7% of the prior, 30 misses to 0.4%.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"

namespace mayflower {

struct CountResult {
    std::uint64_t count = 0;       // |Omega| under the given constraints
    std::size_t   peakStates = 0;  // largest live layer
    std::uint64_t stateVisits = 0; // states processed across all cells
    std::uint64_t edges = 0;       // transitions relaxed
    std::vector<std::uint32_t> layerSizes;   // live states entering each cell layer
};

// Per-cell filter plus the per-placement gate.
//
// `allowH` and `allowV` are indexed [cellIndex * nLengths + lengthIndex], where
// lengthIndex indexes Instance::distinctLengths() and cellIndex is the ship's
// origin (leftmost cell for horizontal, topmost for vertical). Leaving them
// empty permits every placement, which is the observation-free case.
struct Constraints {
    std::vector<CellConstraint> cells;
    std::vector<std::uint8_t>   allowH;
    std::vector<std::uint8_t>   allowV;

    [[nodiscard]] bool gated() const { return !allowH.empty(); }
};

// Build the full constraint set from an ordered observation record. This is the
// only supported way to obtain sunk-ship semantics.
Constraints constraintsFrom(const Instance& inst, const History& history);

CountResult countConfigurations(const Instance& inst, const Constraints& constraints);

// Per-cell constraints only, with every placement permitted.
CountResult countConfigurations(const Instance& inst,
                                const std::vector<CellConstraint>& cells);

CountResult countConfigurations(const Instance& inst);

// Optimisation ladder. V0 is countConfigurations above, kept frozen as the
// reference. V1 packs the state into one uint64, tags liveness with an epoch in
// the spare high bits, and pre-sizes the table. Both must agree exactly;
// tests/test_ladder.cpp enforces it.
//
// The packed key needs 3*height + 3 + fleetBits bits and reserves 16 for the
// epoch, so tall boards fall outside it. Check first.
bool fastPathSupports(const Instance& inst);
CountResult countConfigurationsFast(const Instance& inst, const Constraints& constraints,
                                    std::size_t capacityHint = 0);
CountResult countConfigurationsFast(const Instance& inst);

// Exact occupancy marginal for one cell, as a constrained count.
//
// Cost is O(cells) full counts for a whole heatmap, or 15 under D4 symmetry.
// `occupancyMap` computes every cell marginal in one forward and one backward
// pass and should be preferred; this function remains as its reference.
std::uint64_t occupancyCount(const Instance& inst, int row, int col);

// Every cell's occupancy count, from one forward and one backward sweep.
// Returns a row-major vector of length cellCount(). `total` receives |Omega|.
//
// Invariant: the returned counts sum to shipCells() * total exactly.
std::vector<std::uint64_t> occupancyMap(const Instance& inst,
                                        const Constraints& constraints,
                                        std::uint64_t& total);

std::vector<std::uint64_t> occupancyMap(const Instance& inst, std::uint64_t& total);

// ---------------------------------------------------------------------------
// Placement flows.
//
// Every configuration containing a given ship placement is exactly the set of
// lattice paths through that placement's START edge, so weighting the edge by
// F[source] * B[destination] counts them. One forward-backward sweep therefore
// yields, for every one of the 600 placements on the standard board, how many
// configurations contain it.
//
// Cell occupancy, per-length marginals and the one-ply outcome distribution all
// fall out of these numbers.
// ---------------------------------------------------------------------------

struct LatticeFlows {
    std::uint64_t total = 0;
    std::vector<std::uint64_t> occupancy;   // per cell, row-major
    std::vector<std::uint64_t> placement;   // per placement slot
};

// Slot layout: cell * (2 * nLengths) + (horizontal ? 0 : nLengths) + lengthIndex,
// where lengthIndex indexes Instance::distinctLengths() and the cell is the
// ship's origin.
std::size_t placementSlots(const Instance& inst);
std::size_t placementIndex(const Instance& inst, int row, int col, int lengthIndex,
                           bool horizontal);

LatticeFlows analyse(const Instance& inst, const Constraints& constraints);

// ---------------------------------------------------------------------------
// One-ply outcome distribution.
//
// Shooting an unshot cell yields MISS, a plain HIT, or HIT plus SUNK(L). The
// ship covering the cell sinks exactly when every other cell of it has already
// been shot, which is a property of the placement, so the split follows from the
// placement flows.
//
// Because the outcome is determined by the hidden board, I(B; Y_c) is just the
// entropy of this distribution. That makes exact one-step information gain a
// by-product of the same sweep.
// ---------------------------------------------------------------------------

struct OutcomeDistribution {
    std::uint64_t miss = 0;
    std::uint64_t hit = 0;                    // occupied, ship survives
    std::array<std::uint64_t, 9> sunk{};      // indexed by ship length
    bool shootable = false;                   // false for already-shot cells

    [[nodiscard]] std::uint64_t total() const {
        std::uint64_t t = miss + hit;
        for (std::uint64_t v : sunk) t += v;
        return t;
    }
    // I(B; Y_c) in bits.
    [[nodiscard]] double informationBits() const;
    [[nodiscard]] double hitProbability() const;
};

std::vector<OutcomeDistribution> outcomeDistribution(const Instance& inst,
                                                     const History& history,
                                                     std::uint64_t& total);

// ---------------------------------------------------------------------------
// Exact uniform sampling by unranking.
//
// The lattice is a layered DAG in which every configuration is one source-to-
// sink path. Weighting each edge by the number of completions below it turns
// rank r in [0, |Omega|) into a path, so unrank() is a bijection from ranks to
// configurations. Drawing r uniformly therefore samples Omega uniformly, with
// no rejection and no MCMC.
//
// This is what the board generator must use. Sequential rejection placement
// (place the 5, then the 4, and so on) is not uniform: it over-weights
// configurations that leave room for the later ships.
// ---------------------------------------------------------------------------

struct ShipPlacement {
    int  row = 0;          // topmost cell for vertical, the row for horizontal
    int  col = 0;          // leftmost cell for horizontal, the column for vertical
    int  length = 0;
    bool horizontal = true;
};

class Sampler {
public:
    Sampler(const Instance& inst, const Constraints& constraints);
    explicit Sampler(const Instance& inst);
    ~Sampler();
    Sampler(Sampler&&) noexcept;
    Sampler& operator=(Sampler&&) noexcept;

    [[nodiscard]] std::uint64_t total() const;

    // rank must lie in [0, total()). Throws otherwise.
    [[nodiscard]] std::vector<ShipPlacement> unrank(std::uint64_t rank) const;

    // Number of stored backward-count entries, for memory reporting.
    [[nodiscard]] std::size_t storedEntries() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mayflower
