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
//
// This header is now an umbrella. The declarations live in five focused headers
// and are included here so that no consumer has to change. Reach for the narrow
// one when a translation unit needs only part of this.
#pragma once

#include "mayflower/constraints.hpp"
#include "mayflower/counting.hpp"
#include "mayflower/flows.hpp"
#include "mayflower/outcomes.hpp"
#include "mayflower/sampler.hpp"
