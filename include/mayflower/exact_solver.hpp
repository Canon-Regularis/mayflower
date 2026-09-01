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

// Who the hider is.
//
// Committed: the board was fixed before play and is uniform over the surviving
// set, so a chance node averages. The answer is expected shots.
//
// Adaptive: the hider never commits and answers each shot to hurt most, subject
// to staying consistent with everything already said. A chance node becomes a
// maximum and the answer is the worst case, the Renyi-Ulam version of the game.
// Since the adversary is free to keep any surviving board alive, this is exactly
// the guarantee a searcher can make with no distributional assumption at all.
enum class Adversary { Committed, Adaptive };

struct ExactSolution {
    double expectedShots = 0;
    int optimalFirstShot = -1;
    std::uint64_t configurations = 0;
    std::uint64_t memoStates = 0;
    double seconds = 0;
    std::uint64_t nodesExpanded = 0;   // states whose children were generated
    std::uint64_t cellsPruned = 0;     // candidate shots the bound rejected outright
    std::uint64_t branchesCut = 0;     // chance branches abandoned part-way
};

// Pruning strength. Each level adds one mechanism to the one before, so a
// measurement can attribute the change rather than merely observe it.
//
// None    the original search: reject a cell whose optimistic value already
//         reaches the incumbent, then sum branches and stop once the partial sum
//         does. Cells in index order. This is the reference.
// Bounds  star1's chance-node bound. Branches not yet evaluated are charged at
//         their admissible floor instead of at zero, so the running value is a
//         valid lower bound throughout and cuts earlier. Cells still in index
//         order.
// Star1   Bounds, plus move ordering: cells in descending hit probability and
//         branches in descending floor. This is the default. On an idle machine
//         it is at least as fast as Bounds on every instance measured, and on
//         the fleet instances it is worth 158x: the m9 self-test runs in 66 s
//         under Star1 and took 10,490 s under Bounds. Pick this default on the
//         expensive instances: on the cheap ones the margin sits inside the
//         run-to-run spread, and one timing run does not resolve it.
enum class Pruning { None, Bounds, Star1 };

ExactSolution solveOptimal(const Instance& inst, std::uint64_t configurationLimit = 60000,
                           Adversary adversary = Adversary::Committed,
                           Pruning pruning = Pruning::Star1);

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

    // Misses fired per game once the record already names the board, which is
    // the point at which the sweep returns 1. Nothing is left to learn there,
    // so these separate a rule that chooses badly under uncertainty from one
    // that stops choosing at all. Information gain does the second: the score
    // is the entropy of the answer, which is zero when the answer is known, so
    // a located ship ties with every empty cell and the tie goes to the lowest
    // index.
    double missesAfterCertainty = 0;
};

PolicyExpectation exactPolicyExpectation(const Instance& inst, Policy& policy,
                                        std::uint64_t seed = 0);

}  // namespace mayflower
