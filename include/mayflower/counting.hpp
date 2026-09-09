// Counting configurations, and the optimisation ladder that does it faster.
//
// V0 is countConfigurations, kept frozen as the reference every later rung is
// measured against. Each rung must return bit-identical counts, since a faster
// wrong answer is not a speedup.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mayflower/constraints.hpp"

namespace mayflower {

struct CountResult {
    std::uint64_t count = 0;       // |Omega| under the given constraints
    std::size_t   peakStates = 0;  // largest live layer
    std::uint64_t stateVisits = 0; // states processed across all cells
    std::uint64_t edges = 0;       // transitions relaxed
    std::vector<std::uint32_t> layerSizes;   // live states entering each cell layer
};

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

}  // namespace mayflower
