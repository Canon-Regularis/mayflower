// What a policy costs, priced exactly.
//
// This shares nothing with the solver beside it: no World, no Solver, no memo,
// no scoring rule. It drives the sampler, the game loop and the counting sweep
// instead, and it was the only reason src/search/exact_solver.cpp included
// game.hpp, which is what pulled the game loop and the board bank into the core
// library's include graph.
//
// The measurement it exists for: every configuration is enumerated and played
// once, so the expectation is exact rather than sampled, and the waste it
// reports is the misses a policy fires after the record already determines the
// board.
#include "mayflower/exact_solver.hpp"
#include "mayflower/game.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"

namespace mayflower {

namespace {

// Replay a finished record, counting the misses fired after the sweep first
// returns 1. Prefixes are recounted rather than tracked incrementally: these
// instances hold at most a few hundred configurations, and reusing the ordered
// constraint path keeps the SUNK semantics identical to the ones under test.
int missesAfterCertainty(const Instance& inst, const History& full) {
    History prefix(inst);
    int wasted = 0;
    bool certain = false;
    for (int cell : full.sequence()) {
        if (certain && full.outcome(cell) == Outcome::Miss) ++wasted;
        prefix.add(cell / inst.width, cell % inst.width, full.outcome(cell),
                   full.sunkLength(cell));
        if (!certain && countConfigurations(inst, constraintsFrom(inst, prefix)).count == 1)
            certain = true;
    }
    return wasted;
}

}  // namespace

PolicyExpectation exactPolicyExpectation(const Instance& inst, Policy& policy,
                                        std::uint64_t seed) {
    const auto t0 = std::chrono::steady_clock::now();
    const Sampler sampler(inst);
    const std::uint64_t total = sampler.total();

    // Averaging over no configurations gave 0/0. A NaN expectation is worse
    // than a refusal: it compares false against every bound a caller might
    // check it against, so a policy that cannot be evaluated looks like one
    // that beat everything.
    if (total == 0)
        throw std::invalid_argument(inst.describe() +
                                    " admits no configuration to average a policy over");

    PolicyExpectation out;
    out.configurations = total;
    out.best = inst.cellCount() + 1;
    std::uint64_t sum = 0;
    std::uint64_t wasted = 0;
    for (std::uint64_t r = 0; r < total; ++r) {
        History record(inst);
        const int shots = playGameTraced(inst, sampler.unrank(r), policy, seed, record).shots;
        sum += static_cast<std::uint64_t>(shots);
        wasted += static_cast<std::uint64_t>(missesAfterCertainty(inst, record));
        out.worst = std::max(out.worst, shots);
        out.best = std::min(out.best, shots);
    }
    out.expectedShots = static_cast<double>(sum) / static_cast<double>(total);
    out.missesAfterCertainty = static_cast<double>(wasted) / static_cast<double>(total);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

}  // namespace mayflower
