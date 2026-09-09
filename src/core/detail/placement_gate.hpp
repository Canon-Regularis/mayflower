// Whether a ship starts at a cell, once.
//
// Four sweeps carried this pair of conditions, written out identically, and the
// three line comment on the vertical branch was copied verbatim into all four.
// profile_dp_fast.cpp carries a fifth that hoists the same predicate out of its
// state loop, which is the rung's optimisation and stays there; it uses these
// for the geometry.
//
// Each predicate answers two questions. First, geometry: does a ship of this
// length fit from this cell. Second, the record: does the observation history
// allow a ship of this length to start here, which is the per placement gate
// that constraintsFrom builds. A null gate means unconstrained, not empty.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mayflower::detail {

[[nodiscard]] inline bool startsHorizontal(int col, int length, int width,
                                           const std::uint8_t* allowH, std::size_t li) {
    return col + length <= width && (allowH == nullptr || allowH[li]);
}

// A length 1 ship has one placement, not two, so only the horizontal branch
// emits it. A sweep that emitted it from both branches returned 2^k times the
// correct count. The fixed case lists did not detect this because every case
// used ships of length two or more, where the two branches cannot disagree.
[[nodiscard]] inline bool startsVertical(int row, int length, int height,
                                         const std::uint8_t* allowV, std::size_t li) {
    return length > 1 && row + length <= height && (allowV == nullptr || allowV[li]);
}

}  // namespace mayflower::detail
