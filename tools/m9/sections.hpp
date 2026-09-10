// What each extension exposes to the dispatcher and the self test.
//
// tools/m9.cpp held all eight in one anonymous namespace, so nothing stated
// which of them the self test actually reaches into. These are the seams.
#pragma once

#include "mayflower/instance.hpp"

namespace mayflower::m9 {

// One per extension. Each prints its own section.
void adversary();
void phaseTransition();
void adaptivityGap();
void bimaruCost();
void salvo();
void noisy();

// Every invariant, across all eight. Returns a process exit code.
int selfTest();

// The adaptivity result, needed by the self test as well as its own section.
struct NonAdaptive {
    double optimal = 0;
    double greedy = 0;
    std::uint64_t configurations = 0;
};
NonAdaptive nonAdaptiveOptimum(const Instance& inst);

}  // namespace mayflower::m9
