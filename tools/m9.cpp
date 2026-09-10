// The eight mathematical extensions, dispatched.
//
// This file was 838 lines holding all eight experiments, their shared
// enumerator and a ninety line self test. Each extension now owns a file under
// tools/m9 and this is only the argument handling.
// m9: the mathematical extensions that reuse the engine directly.
//
//   1. The adaptive adversary. Expected shots assume the board was fixed before
//      play. Against a hider who never commits and answers to hurt most, the
//      same searcher faces a worst case instead.
//
//   2. Constraint density. Feed both a counting sweep and a backtracking search
//      observation records that no board produced, and sweep how constrained
//      they are. The search shows the easy-hard-easy profile of random
//      satisfiability. The sweep does not, and the reason is structural.


#include <string>

#include "m9/sections.hpp"

using namespace mayflower::m9;

int main(int argc, char** argv) {
    // No argument runs everything. A name runs one section, since the belief MDP
    // and the density sweep are minutes apart in cost.
    const std::string only = argc > 1 ? argv[1] : "";
    const bool all = only.empty();
    if (all) {
        std::printf("Mayflower, mathematical extensions\n");
        std::printf("==================================\n\n");
    }
    if (all || only == "adversary")  adversary();
    if (all || only == "density")    phaseTransition();
    if (all) std::printf("\n");
    if (all || only == "adaptivity") adaptivityGap();
    if (all || only == "bimaru")     bimaruCost();
    if (all || only == "salvo")      salvo();
    if (all || only == "noisy")      noisy();
    if (only == "selftest")          return selfTest();
    return 0;
}
