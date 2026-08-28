// Counting under the no-touching ruleset: distinct ships may not share an edge
// or a corner. The printed puzzle uses it, and it takes the standard instance
// from 15,046,987,768 configurations to 1,925,751,392.
//
// The standard profile cannot express it. Sweeping column-major, the decided 8-neighbours of cell (r,c) are
//
//     (r-1, c-1)   (r, c-1)   (r+1, c-1)   (r-1, c)
//
// and the residual extensions determine none of them. A horizontal ship ending
// at column c-1 leaves ext[r] == 0 while (r,c-1) is occupied, so the sweep has
// to carry the previous column's occupancy explicitly.
//
// State.
//   ext[row]   in 0..maxLen-1   as in profile_dp.hpp, 3 bits per row
//   colBits    H bits           slots below the row cursor hold the CURRENT
//                               column, slots at or above it hold the PREVIOUS
//                               one. One word serves both because the slot
//                               prev[r] vacates is exactly the slot cur[r] wants.
//   carry      1 bit            prev[r-1], which colBits has already overwritten
//                               with cur[r-1]. Reset to 0 at each column start.
//   vrem, fleet                 as in profile_dp.hpp
//
// Every 8-adjacent pair has one member decided strictly before the other, so
// checking a cell against its decided neighbours as it is placed catches every
// touching pair, and ship halos need no representation. What each transition
// checks differs only in which neighbour belongs to the same ship:
//
//   horizontal continuation   (r-1,c-1), (r+1,c-1), (r-1,c)   [(r,c-1) is ours]
//   vertical continuation     (r-1,c-1), (r,c-1), (r+1,c-1)   [(r-1,c) is ours]
//   either start              all four
//   empty                     nothing
//
// The whole state packs into one uint64, which caps the height. Check with
// noTouchSupports() first.
#pragma once

#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace mayflower {

// False when the packed key would not fit in 64 bits.
[[nodiscard]] bool noTouchSupports(const Instance& inst);

// Bits the packed key needs for this instance, for diagnostics.
[[nodiscard]] int noTouchKeyBits(const Instance& inst);

CountResult countNoTouch(const Instance& inst, const Constraints& constraints);
CountResult countNoTouch(const Instance& inst, const std::vector<CellConstraint>& cells);
CountResult countNoTouch(const Instance& inst);

}  // namespace mayflower
