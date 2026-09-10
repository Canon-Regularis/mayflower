// What every sweep does before it starts, and the constraint set that means
// "no observations".
//
// The two idioms below were written out fifteen times across the six sweeps:
// the validate-then-size-check preamble seven times, and the free constraint
// set eight. Both are the kind of thing that is correct in every copy until one
// copy is edited, and the size check in particular carries an error message
// that has to read the same wherever it comes from.
//
// Internal to src/core. Nothing here changes a public signature: the sweeps
// keep the same parameters and throw the same exception with the same text.
#pragma once

#include <cstddef>
#include <stdexcept>

#include "mayflower/constraints.hpp"
#include "mayflower/instance.hpp"

namespace mayflower::detail {

// The precondition every sweep shares. Validate the instance, then check the
// per-cell vector is the size the instance implies.
inline void checkConstraints(const Instance& inst, const Constraints& constraints) {
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");
}

// Every cell free, which is the observation-free case. Leaving allowH and
// allowV empty is what permits every placement; see Constraints::gated().
[[nodiscard]] inline Constraints freeConstraints(const Instance& inst) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return c;
}

}  // namespace mayflower::detail
