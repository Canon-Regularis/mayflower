// Ladder rungs V2 and V3. See src/core/profile_dp_blocked.cpp for the argument.
//
// V2 is the radix-partitioned merge at one thread; V3 is the same work spread
// over several. Both must return counts bit-identical to V0, which integer
// addition makes automatic: buckets partition the destination keys, so no two
// merges touch one counter, and the order they run in cannot matter.
#pragma once

#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace mayflower {

// False when the packed key plus the radix would not fit in 64 bits.
[[nodiscard]] bool blockedPathSupports(const Instance& inst);

CountResult countConfigurationsBlocked(const Instance& inst, const Constraints& constraints,
                                       int threads = 1);

// Per-cell constraints only, matching the shape countConfigurations, countNoTouch
// and weightedCount all offer. This rung was the odd one out.
CountResult countConfigurationsBlocked(const Instance& inst,
                                       const std::vector<CellConstraint>& cells,
                                       int threads = 1);
CountResult countConfigurationsBlocked(const Instance& inst, int threads = 1);

}  // namespace mayflower
