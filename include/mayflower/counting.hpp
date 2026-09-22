// Counting configurations, and the optimisation ladder that does it faster.
//
// V0 is countConfigurations, kept frozen as the reference every later rung is
// measured against. Each rung must return bit-identical counts, since a faster
// wrong answer is not a speedup.
//
// Nothing is implemented in a counting.cpp, because there is none. The rungs
// are named after themselves: countConfigurations is in src/core/profile_dp.cpp,
// the fast path in profile_dp_fast.cpp and the blocked rungs in
// profile_dp_blocked.cpp. Every other public header here pairs with a source
// file of its own name; this one is declared by the ladder and implemented by
// its rungs.
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
    // Live states entering each cell layer, one entry per cell, filled by
    // every rung. tools/report_data.cpp publishes it and render_report draws
    // the layer-profile figure from it, so a rung that left it empty would
    // give that figure a blank plate with nothing to explain why.
    // countConfigurationsFast was that rung until the omission was found.
    std::vector<std::uint32_t> layerSizes;

    // False when an accumulator passed 2^64 and `count` is the true answer
    // modulo 2^64. The counting path is unsigned, so it wraps in silence: no
    // trap, no flag, and a plausible nineteen-digit answer.
    //
    // This is reachable on an instance validate() accepts. 16x8 with sixteen
    // 1-ships is 128 cells and 16 ship cells, every clause passes, and the true
    // count is C(128,16) = 9.334e19. The sweep used to return
    // 1109300832714419320, which is that value modulo 2^64, with nothing to say
    // so. Fifteen 1-ships fit and sixteen do not, so the boundary is crossed by
    // adding one ship to a legal fleet.
    //
    // weightedCount already reported this class through maxLayerSum; the
    // integer path, which is the one that publishes 15,046,987,768, had no
    // equivalent. See tests/test_counting.cpp.
    bool exact = true;
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
