// The unranking sampler. unrank must be a bijection from [0, |Omega|) onto the
// configuration set, which proves uniformity outright.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"
#include "oracle/brute_force.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

template <typename T>
void checkEq(T got, T want, const std::string& what) {
    ++gChecks;
    if (got != want) {
        ++gFailures;
        std::printf("  FAIL  %s: got %llu, want %llu\n", what.c_str(),
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

using mayflower::Instance;
using mayflower::History;
using mayflower::Outcome;
using mayflower::Sampler;
using mayflower::ShipPlacement;

oracle::BoardShips toMasks(const std::vector<ShipPlacement>& ships, int W) {
    oracle::BoardShips out;
    out.reserve(ships.size());
    for (const ShipPlacement& s : ships) {
        oracle::Mask m = 0;
        for (int k = 0; k < s.length; ++k) {
            const int cell = s.horizontal ? s.row * W + s.col + k
                                          : (s.row + k) * W + s.col;
            m |= oracle::Mask{1} << cell;
        }
        out.push_back(m);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<oracle::BoardShips> canonical(std::vector<oracle::BoardShips> boards) {
    for (auto& b : boards) std::sort(b.begin(), b.end());
    std::sort(boards.begin(), boards.end());
    return boards;
}

// ---------------------------------------------------------------------------

void testUnrankBijection() {
    std::printf("[unrank bijection, exhaustive]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {
        {4, 4, {3, 2}},
        {5, 5, {3, 2, 2}},
        {5, 5, {4, 3, 2}},
        {6, 6, {3, 3, 2}},
    };
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        const Sampler sampler(inst);
        const std::uint64_t total = sampler.total();
        checkEq(total, mayflower::countConfigurations(inst).count, "sampler total " + inst.describe());

        std::vector<oracle::BoardShips> produced;
        produced.reserve(static_cast<std::size_t>(total));
        for (std::uint64_t r = 0; r < total; ++r)
            produced.push_back(toMasks(sampler.unrank(r), c.w));

        const auto want = canonical(oracle::enumerateBoards(c.w, c.h, c.fleet));
        const auto got  = canonical(std::move(produced));

        checkEq(static_cast<std::uint64_t>(got.size()), static_cast<std::uint64_t>(want.size()),
                "board count " + inst.describe());
        check(got == want, "unrank enumerates exactly the configuration set, once each: " +
                               inst.describe());

        // Distinctness follows from the set equality above plus equal sizes, but
        // assert it directly so a duplicate cannot hide behind a missing board.
        auto uniq = got;
        uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
        checkEq(static_cast<std::uint64_t>(uniq.size()), total, "all ranks distinct " + inst.describe());

        std::printf("  %-14s %6llu ranks, bijective onto the oracle's boards\n",
                    inst.describe().c_str(), static_cast<unsigned long long>(total));
    }
}

void testFleetComposition() {
    std::printf("[fleet composition]\n");
    const Instance inst(6, 6, {4, 3, 3, 2});
    const Sampler sampler(inst);
    const std::uint64_t total = sampler.total();
    for (std::uint64_t r = 0; r < total; r += 997) {
        const auto ships = sampler.unrank(r);
        std::vector<int> lengths;
        for (const auto& s : ships) lengths.push_back(s.length);
        std::sort(lengths.begin(), lengths.end());
        std::vector<int> want = inst.fleet;
        std::sort(want.begin(), want.end());
        check(lengths == want, "sampled fleet matches at rank " + std::to_string(r));
    }
    std::printf("  %llu ranks sampled, every one realises {2,3,3,4}\n",
                static_cast<unsigned long long>((total + 996) / 997));
}

// Under a history, every sampled board must reproduce that exact history.
void testSamplingUnderObservations() {
    std::printf("[sampling under observations]\n");
    const int W = 5, H = 5;
    const std::vector<int> fleet = {3, 2, 2};
    const Instance inst(W, H, fleet);

    History h(inst);
    h.add(1, 1, Outcome::Hit);
    h.add(0, 3, Outcome::Miss);
    h.add(2, 1, Outcome::Hit);
    h.add(3, 1, Outcome::Sunk, 3);

    const auto constraints = mayflower::constraintsFrom(inst, h);
    const Sampler sampler(inst, constraints);
    const std::uint64_t total = sampler.total();
    checkEq(total, mayflower::countConfigurations(inst, constraints).count,
            "constrained sampler total");
    check(total > 0, "history is feasible");

    const std::vector<int> shots = {1 * W + 1, 0 * W + 3, 2 * W + 1, 3 * W + 1};
    const std::vector<oracle::Observation> observed = {
        {oracle::Outcome::Hit, 0}, {oracle::Outcome::Miss, 0},
        {oracle::Outcome::Hit, 0}, {oracle::Outcome::Sunk, 3},
    };

    for (std::uint64_t r = 0; r < total; ++r) {
        const auto board = toMasks(sampler.unrank(r), W);
        if (oracle::simulate(board, shots) != observed) {
            check(false, "sampled board at rank " + std::to_string(r) + " replays the history");
            break;
        }
    }
    ++gChecks;
    std::printf("  all %llu constrained ranks replay the history exactly\n",
                static_cast<unsigned long long>(total));

    // The posterior support equals the oracle's, as sets.
    std::vector<oracle::BoardShips> produced;
    for (std::uint64_t r = 0; r < total; ++r) produced.push_back(toMasks(sampler.unrank(r), W));
    std::vector<oracle::BoardShips> want;
    for (const auto& b : oracle::enumerateBoards(W, H, fleet))
        if (oracle::simulate(b, shots) == observed) want.push_back(b);
    check(canonical(std::move(produced)) == canonical(std::move(want)),
          "constrained support matches the oracle posterior");
}

// Marginals recovered by counting sampled occupancy must equal the exact
// marginals, since the sampler visits every configuration exactly once.
void testSamplerAgreesWithMarginals() {
    std::printf("[sampler vs exact marginals]\n");
    const Instance inst(5, 5, {3, 2, 2});
    const Sampler sampler(inst);
    const std::uint64_t total = sampler.total();

    std::vector<std::uint64_t> counted(static_cast<std::size_t>(inst.cellCount()), 0);
    for (std::uint64_t r = 0; r < total; ++r) {
        for (const auto& s : sampler.unrank(r)) {
            for (int k = 0; k < s.length; ++k) {
                const int cell = s.horizontal ? s.row * inst.width + s.col + k
                                              : (s.row + k) * inst.width + s.col;
                ++counted[static_cast<std::size_t>(cell)];
            }
        }
    }
    std::uint64_t exactTotal = 0;
    const auto exact = mayflower::occupancyMap(inst, exactTotal);
    checkEq(exactTotal, total, "totals agree");
    check(counted == exact, "occupancy counted over all ranks equals the exact marginals");
    std::printf("  all %d cells agree across %llu enumerated ranks\n", inst.cellCount(),
                static_cast<unsigned long long>(total));
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testUnrankBijection();
    testFleetComposition();
    testSamplingUnderObservations();
    testSamplerAgreesWithMarginals();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
