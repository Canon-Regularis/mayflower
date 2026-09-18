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
// No declaration lives here. This is the overview, and the five headers below
// are the map: constraints.hpp for the per-cell filter and the placement gate,
// counting.hpp for the sweep itself, flows.hpp for forward-backward marginals,
// outcomes.hpp for the one-ply channel, sampler.hpp for the unranker.
//
// It was an umbrella when the split landed, so that no consumer had to change,
// and it told the reader to reach for the narrow header instead. Nobody did:
// every one of the 31 consumers kept taking all five, and so did the four
// implementations that exist to define them. The consumers have since been
// migrated to what they name, which left this header with no includer at all
// apart from the self-test that compiles it. Including it is not wrong, but it
// is wider than anything has needed so far, so prefer the narrow one.
#pragma once

#include "mayflower/constraints.hpp"
#include "mayflower/counting.hpp"
#include "mayflower/flows.hpp"
#include "mayflower/outcomes.hpp"
#include "mayflower/sampler.hpp"
