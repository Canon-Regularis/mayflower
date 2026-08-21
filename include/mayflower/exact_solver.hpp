// Exact optimal play on small instances.
//
// The belief state is the posterior support, and the posterior is uniform on it,
// so the value of a state is the expected number of shots left under optimal
// play. Solving that recursion exactly gives the true optimum, which turns the
// optimality gap of a heuristic from an estimate into a measurement.
//
// The state space grows quickly, so this is for boards small enough to enumerate
// (roughly up to 32 cells and a few hundred configurations). Its purpose is to
// calibrate heuristics where the answer is knowable, not to play 10x10.
#pragma once

#include <cstdint>
#include <vector>

#include "mayflower/game.hpp"
#include "mayflower/instance.hpp"

namespace mayflower {

struct ExactSolution {
    double expectedShots = 0;
    int optimalFirstShot = -1;
    std::uint64_t configurations = 0;
    std::uint64_t memoStates = 0;
    double seconds = 0;
};

ExactSolution solveOptimal(const Instance& inst, std::uint64_t configurationLimit = 60000);

// Expected shots for a policy, averaged over every configuration. Exact, with no
// sampling, because the whole space is enumerated.
//
// The seed is FIXED across boards on purpose. Seeding a stochastic policy from
// the board's identity would give each board its own policy randomisation, which
// measures a family of policies each paired with its own board and can score
// below the single-policy optimum. Holding the seed fixed makes the policy one
// deterministic function of the history, which is what the optimum bounds.
struct PolicyExpectation {
    double expectedShots = 0;
    int worst = 0;
    int best = 0;
    std::uint64_t configurations = 0;
    double seconds = 0;
};

PolicyExpectation exactPolicyExpectation(const Instance& inst, Policy& policy,
                                        std::uint64_t seed = 0);

}  // namespace mayflower
