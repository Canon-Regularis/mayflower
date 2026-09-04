// Differential fuzz over random instances and random ordered histories.
//
// The fixed case lists elsewhere were chosen by hand, and for a long time every
// one of them used ships of length two or more. That is exactly the input where
// the horizontal and vertical branches of a sweep cannot disagree, so a rung
// that emitted a length-1 ship from both branches returned 2^k times the truth
// and every list-based test passed. This draws its instances instead, so the
// shapes nobody thought to write down get covered: single-cell ships, 1xN and
// Nx1 boards, repeated lengths, and fleets that nearly fill the space.
//
// The seed is fixed and taken from the command line, so a run is reproducible
// and a failure can be replayed with the seed it printed. It is a large
// generated case list rather than a search.
//
//     test_fuzz [trials] [seed]

#include "mayflower/notouch.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/profile_dp_blocked.hpp"
#include "mayflower/weighted.hpp"
#include "oracle/brute_force.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace mayflower;

int gFailures = 0;
int gChecks = 0;

void fail(const std::string& what) {
    std::printf("  FAIL  %s\n", what.c_str());
    ++gFailures;
}

std::string describe(const Instance& inst, const History& h) {
    return inst.describe() + " after " + std::to_string(h.size()) + " shots";
}

// Play a real board so the record is one a game could actually produce. A
// history invented cell by cell would mostly be infeasible, and infeasible
// records are the easy case: everything returns zero and agrees.
History playRandomPrefix(const Instance& inst, std::mt19937_64& rng) {
    History hist(inst);
    const Sampler sampler(inst);
    if (sampler.total() == 0) return hist;

    const int W = inst.width;
    const auto truth = sampler.unrank(rng() % sampler.total());
    std::vector<int> shipAt(static_cast<std::size_t>(inst.cellCount()), -1);
    std::vector<int> remaining(truth.size());
    for (std::size_t i = 0; i < truth.size(); ++i) {
        remaining[i] = truth[i].length;
        for (int k = 0; k < truth[i].length; ++k) {
            const int cell = truth[i].horizontal ? truth[i].row * W + truth[i].col + k
                                                 : (truth[i].row + k) * W + truth[i].col;
            shipAt[static_cast<std::size_t>(cell)] = static_cast<int>(i);
        }
    }

    std::vector<int> order(static_cast<std::size_t>(inst.cellCount()));
    for (int i = 0; i < inst.cellCount(); ++i) order[static_cast<std::size_t>(i)] = i;
    std::shuffle(order.begin(), order.end(), rng);

    const int shots = static_cast<int>(rng() % static_cast<unsigned>(inst.cellCount() + 1));
    for (int i = 0; i < shots; ++i) {
        const int cell = order[static_cast<std::size_t>(i)];
        const int ship = shipAt[static_cast<std::size_t>(cell)];
        if (ship < 0) {
            hist.add(cell / W, cell % W, Outcome::Miss);
        } else if (--remaining[static_cast<std::size_t>(ship)] == 0) {
            hist.add(cell / W, cell % W, Outcome::Sunk,
                     truth[static_cast<std::size_t>(ship)].length);
        } else {
            hist.add(cell / W, cell % W, Outcome::Hit);
        }
    }
    return hist;
}

std::vector<oracle::Observation> asOracleRecord(const History& h) {
    std::vector<oracle::Observation> out;
    for (int cell : h.sequence()) {
        oracle::Observation ob;
        switch (h.outcome(cell)) {
            case Outcome::Miss: ob.outcome = oracle::Outcome::Miss; break;
            case Outcome::Hit:  ob.outcome = oracle::Outcome::Hit;  break;
            case Outcome::Sunk:
                ob.outcome = oracle::Outcome::Sunk;
                ob.sunkLength = h.sunkLength(cell);
                break;
        }
        out.push_back(ob);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const int trials = argc > 1 ? std::atoi(argv[1]) : 250;
    const std::uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 0x5EEDu;
    if (trials < 1) {
        std::fprintf(stderr, "trials must be a positive integer\n");
        return 2;
    }

    std::printf("differential fuzz over random instances\n");
    std::printf("=======================================\n");
    std::printf("  %d trials, seed 0x%llX\n", trials, static_cast<unsigned long long>(seed));

    const auto t0 = std::chrono::steady_clock::now();
    std::mt19937_64 rng(seed);
    int instances = 0, withHistory = 0, withLength1 = 0, withSunk = 0;

    for (int t = 0; t < trials; ++t) {
        // Small enough that literal enumeration stays feasible, and deliberately
        // allowing a side of 1.
        const int w = 1 + static_cast<int>(rng() % 5);
        const int h = 1 + static_cast<int>(rng() % 5);
        const int longest = std::max(w, h);

        std::vector<int> fleet;
        const int ships = 1 + static_cast<int>(rng() % 3);
        for (int i = 0; i < ships; ++i)
            fleet.push_back(1 + static_cast<int>(rng() % static_cast<unsigned>(longest)));

        Instance inst;
        try {
            inst = Instance(w, h, fleet);
        } catch (const std::invalid_argument&) {
            continue;   // the draw named a board the engine does not represent
        }

        const History hist = (rng() & 1) ? playRandomPrefix(inst, rng) : History(inst);
        const Constraints cons = constraintsFrom(inst, hist);
        const std::uint64_t reference = countConfigurations(inst, cons).count;

        ++instances;
        if (hist.size() > 0) ++withHistory;
        if (std::find(inst.fleet.begin(), inst.fleet.end(), 1) != inst.fleet.end()) ++withLength1;
        for (int cell : hist.sequence())
            if (hist.outcome(cell) == Outcome::Sunk) { ++withSunk; break; }

        // Literal enumeration, playing the same shot order against every board.
        ++gChecks;
        const auto boards = oracle::enumerateBoards(w, h, inst.fleet);
        const std::uint64_t brute =
            oracle::posteriorCount(boards, hist.sequence(), asOracleRecord(hist));
        if (brute != reference)
            fail(describe(inst, hist) + ": sweep " + std::to_string(reference) +
                 ", enumeration " + std::to_string(brute));

        // Every other rung, where the instance fits it.
        const auto agrees = [&](const char* name, std::uint64_t got) {
            ++gChecks;
            if (got != reference)
                fail(describe(inst, hist) + ": " + name + " " + std::to_string(got) +
                     ", sweep " + std::to_string(reference));
        };
        try { agrees("fast", countConfigurationsFast(inst, cons).count); }
        catch (const std::invalid_argument&) {}
        try { agrees("blocked", countConfigurationsBlocked(inst, cons, 1).count); }
        catch (const std::invalid_argument&) {}
        try {
            const Weights unit;
            const auto r = weightedCount(inst, cons, unit);
            ++gChecks;
            if (!r.exact || r.total != static_cast<double>(reference))
                fail(describe(inst, hist) + ": weighted " + std::to_string(r.total) +
                     " exact=" + (r.exact ? "1" : "0") + ", sweep " +
                     std::to_string(reference));
        } catch (const std::invalid_argument&) {}

        // The marginal identity, which holds whatever the record is.
        ++gChecks;
        std::uint64_t total = 0;
        const auto occ = occupancyMap(inst, cons, total);
        std::uint64_t sum = 0;
        for (std::uint64_t v : occ) sum += v;
        if (sum != static_cast<std::uint64_t>(inst.shipCells()) * total)
            fail(describe(inst, hist) + ": marginals sum to " + std::to_string(sum) +
                 ", not " + std::to_string(inst.shipCells()) + " x " + std::to_string(total));

        // The no-touching sweep against its own enumerator, unconstrained.
        if (hist.size() == 0 && noTouchSupports(inst)) {
            ++gChecks;
            const std::uint64_t nt = countNoTouch(inst).count;
            const std::uint64_t ntBrute = oracle::bruteForceCountNoTouch(w, h, inst.fleet);
            if (nt != ntBrute)
                fail(inst.describe() + ": no-touching " + std::to_string(nt) +
                     ", enumeration " + std::to_string(ntBrute));
            if (nt > reference)
                fail(inst.describe() + ": no-touching exceeds the touching count");
        }
    }

    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  %d instances, %d with a record, %d with a SUNK, %d holding a length-1 ship\n",
                instances, withHistory, withSunk, withLength1);
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    if (gFailures)
        std::printf("replay with: test_fuzz %d 0x%llX\n", trials,
                    static_cast<unsigned long long>(seed));
    return gFailures == 0 ? 0 : 1;
}
