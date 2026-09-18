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

// Every cell free, which is the observation-free case. Leaving allowH and
// allowV empty is what permits every placement; see gated() above.
//
// This existed as detail::freeConstraints in src/core/detail/entry.hpp, which
// retired eight copies inside src/core and could retire no more, because
// neither tools/ nor tests/ has src/ on its include path. So the idiom stayed
// written out at twenty-six further sites, several of them with the cell count
// hardcoded as 16, 25, 64 or 100 rather than taken from the instance, which is
// the drift the helper exists to prevent. It is public now because the callers
// that need it are.
[[nodiscard]] inline Constraints freeConstraints(const Instance& inst) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return c;
}

}  // namespace mayflower
