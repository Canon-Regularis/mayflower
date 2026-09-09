// What the sweep is given: a per-cell filter and a per-placement gate.
//
// This is where the ordered observation record turns into something a sweep
// with no notion of time can consume. constraintsFrom is the only supported
// route to sunk-ship semantics, and every other header here depends on this
// one.
#pragma once

#include <cstdint>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"

namespace mayflower {

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

}  // namespace mayflower
