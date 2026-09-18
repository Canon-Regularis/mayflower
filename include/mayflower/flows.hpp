// Marginals and placement flows from one forward-backward pass.
//
// Every configuration containing a placement is the set of lattice paths
// through that placement's START edge, so one pass prices all 600 placements on
// the standard board rather than paying for a constrained recount each.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mayflower/constraints.hpp"

namespace mayflower {

// Exact occupancy marginal for one cell, as a constrained count.
//
// Cost is O(cells) full counts for a whole heatmap, or 15 under D4 symmetry.
// `occupancyMap` computes every cell marginal in one forward and one backward
// pass and should be preferred; this function remains as its reference.
std::uint64_t occupancyCount(const Instance& inst, int row, int col);

// Every cell's occupancy count, from one forward and one backward sweep.
//
// Invariant: counts sum to shipCells() * total exactly.
//
// Both numbers come back together. This used to return the vector and write
// |Omega| through a std::uint64_t& out-parameter, which made the caller declare
// an uninitialised total on the line above and left the two free to drift
// apart. LatticeFlows below is the same pair plus the placement flows and has
// always been a struct, so the two shapes disagreed inside one header.
struct OccupancyMap {
    std::uint64_t total = 0;                // |Omega| under the constraints
    std::vector<std::uint64_t> counts;      // per cell, row-major
};

OccupancyMap occupancyMap(const Instance& inst, const Constraints& constraints);
OccupancyMap occupancyMap(const Instance& inst);

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

}  // namespace mayflower
