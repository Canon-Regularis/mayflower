// Certified bounds.
//
// Everything here is either exact by construction or explicitly labelled as an
// upper bound on a quantity we cannot yet pin down. A rung whose direction is
// unproven says so.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mayflower/instance.hpp"

namespace mayflower {

// Blocking number beta(L): the fewest cells that meet every placement of a
// length-L ship on a W x H board.
//
// Shooting fewer than beta(L) cells always leaves some length-L placement
// completely unshot, so beta(L) is exactly the number of shots needed to
// guarantee first contact with a lone length-L ship.
//
// Computed as W*H minus the largest cell set containing no L consecutive cells
// in any row or column, by a row-sweep DP whose state is the trailing vertical
// run in each column plus the running horizontal run.
struct BlockingResult {
    int blocking = 0;       // beta(L)
    int largestFreeSet = 0; // W*H - beta(L)
    double seconds = 0;
};

BlockingResult blockingNumber(int width, int height, int length);

// A witness: a set of `blocking` cells meeting every length-L placement.
std::vector<int> blockingWitness(int width, int height, int length);

}  // namespace mayflower
