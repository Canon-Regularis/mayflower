// Optimisation ladder equality.
//
// Every rung must return bit-identical counts, since a faster wrong answer is
// not a speedup. Counts are integers, so equality is exact and no tolerance is
// involved.

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/profile_dp_blocked.hpp"

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

using namespace mayflower;

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

// Every rung, against V0, on the same constraints. Counts are integers, so this
// is exact equality and there is no tolerance to argue about.
bool agree(const Instance& inst, const Constraints& c, const std::string& label) {
    const std::uint64_t v0 = countConfigurations(inst, c).count;
    const std::uint64_t v1 = countConfigurationsFast(inst, c).count;
    ++gChecks;
    if (v0 != v1) {
        ++gFailures;
        std::printf("  FAIL  %s: V0 %llu, V1 %llu\n", label.c_str(),
                    static_cast<unsigned long long>(v0), static_cast<unsigned long long>(v1));
        return false;
    }

    if (!blockedPathSupports(inst)) return true;

    // V2 is the radix-partitioned merge at one thread, V3 the same work spread
    // over several. Buckets partition the destination keys, so no two merges
    // touch one counter and the thread count cannot change the answer.
    const std::uint64_t v2 = countConfigurationsBlocked(inst, c, 1).count;
    ++gChecks;
    if (v0 != v2) {
        ++gFailures;
        std::printf("  FAIL  %s: V0 %llu, V2 %llu\n", label.c_str(),
                    static_cast<unsigned long long>(v0), static_cast<unsigned long long>(v2));
        return false;
    }

    for (int threads : {2, 4, 7}) {
        const std::uint64_t v3 = countConfigurationsBlocked(inst, c, threads).count;
        ++gChecks;
        if (v0 != v3) {
            ++gFailures;
            std::printf("  FAIL  %s: V0 %llu, V3(%d threads) %llu\n", label.c_str(),
                        static_cast<unsigned long long>(v0), threads,
                        static_cast<unsigned long long>(v3));
            return false;
        }
    }
    return true;
}

void testUnconstrained() {
    std::printf("[unconstrained instances]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {
        {4, 4, {3, 2}},   {5, 5, {3, 2, 2}}, {5, 5, {4, 3, 2}}, {6, 6, {3, 3, 2}},
        {6, 6, {4, 3, 2}}, {5, 5, {3, 3, 2, 2}}, {4, 6, {3, 2}}, {7, 5, {4, 3, 2}},
        {8, 8, {5, 4, 3, 3, 2}},
        // Length 1 is where the rungs can differ without any of them being
        // obviously wrong: a single cell has one placement, and a rung that
        // emits it from both the horizontal and the vertical branch returns
        // 2^k times the truth. Every case above has L >= 2, so this test
        // asserted bit-identical output over inputs that could not distinguish
        // them.
        {4, 4, {1, 1}}, {5, 4, {3, 1}}, {3, 3, {1, 1, 1}},
    };
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        if (!fastPathSupports(inst)) continue;
        Constraints free;
        free.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
        if (agree(inst, free, inst.describe()))
            std::printf("  %-18s %14llu  identical\n", inst.describe().c_str(),
                        static_cast<unsigned long long>(countConfigurationsFast(inst).count));
    }
}

// The pinned order-dependence cases must agree too, so a rung cannot quietly
// lose the ordered sunk semantics.
void testPinnedHistories() {
    std::printf("[pinned histories]\n");
    const Instance inst(5, 5, {3, 2, 2});

    struct Shot { int r, c; Outcome o; int len; };
    struct Case { const char* label; std::uint64_t expected; std::vector<Shot> shots; };
    const std::vector<Case> cases = {
        {"order A", 41, {{1,1,Outcome::Hit,0},{2,1,Outcome::Hit,0},{3,3,Outcome::Miss,0},
                         {1,2,Outcome::Hit,0},{2,0,Outcome::Sunk,2},{0,0,Outcome::Miss,0},
                         {4,1,Outcome::Miss,0}}},
        {"order B", 53, {{1,2,Outcome::Hit,0},{1,1,Outcome::Hit,0},{4,1,Outcome::Miss,0},
                         {3,3,Outcome::Miss,0},{2,0,Outcome::Hit,0},{0,0,Outcome::Miss,0},
                         {2,1,Outcome::Sunk,2}}},
        {"overcount case", 22, {{4,1,Outcome::Hit,0},{3,4,Outcome::Miss,0},{3,1,Outcome::Sunk,2},
                                {4,3,Outcome::Miss,0},{2,1,Outcome::Hit,0},{4,4,Outcome::Miss,0},
                                {4,2,Outcome::Miss,0},{0,0,Outcome::Miss,0},{1,3,Outcome::Hit,0}}},
    };

    for (const Case& c : cases) {
        History h(inst);
        for (const Shot& s : c.shots) h.add(s.r, s.c, s.o, s.len);
        const Constraints cons = constraintsFrom(inst, h);
        const std::uint64_t v1 = countConfigurationsFast(inst, cons).count;
        check(v1 == c.expected,
              std::string(c.label) + " gives " + std::to_string(c.expected) + " on the fast path");
        agree(inst, cons, c.label);
        std::printf("  %-16s %4llu  identical\n", c.label, static_cast<unsigned long long>(v1));
    }
}

// Random ordered histories, replayed against a known board so they stay
// feasible, then compared rung against rung.
void testFuzzedHistories() {
    std::printf("[fuzzed histories]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {{5, 5, {3, 2, 2}}, {6, 6, {4, 3, 2}}, {6, 5, {3, 3, 2}}};

    Rng rng(0x1234ABCD);
    int cases_run = 0;
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        if (!fastPathSupports(inst)) continue;
        const Sampler sampler(inst);

        for (int trial = 0; trial < 60; ++trial) {
            const auto truth = sampler.unrank(rng.next() % sampler.total());
            std::vector<int> shipAt(static_cast<std::size_t>(inst.cellCount()), -1);
            std::vector<int> remaining(truth.size(), 0);
            for (std::size_t i = 0; i < truth.size(); ++i) {
                remaining[i] = truth[i].length;
                for (int k = 0; k < truth[i].length; ++k) {
                    const int cell = truth[i].horizontal
                                         ? truth[i].row * inst.width + truth[i].col + k
                                         : (truth[i].row + k) * inst.width + truth[i].col;
                    shipAt[static_cast<std::size_t>(cell)] = static_cast<int>(i);
                }
            }

            std::vector<int> pool(static_cast<std::size_t>(inst.cellCount()));
            for (int i = 0; i < inst.cellCount(); ++i) pool[static_cast<std::size_t>(i)] = i;
            for (int i = inst.cellCount() - 1; i > 0; --i)
                std::swap(pool[static_cast<std::size_t>(i)],
                          pool[static_cast<std::size_t>(rng.below(i + 1))]);

            History h(inst);
            const int shots = 2 + rng.below(10);
            for (int i = 0; i < shots; ++i) {
                const int cell = pool[static_cast<std::size_t>(i)];
                const int ship = shipAt[static_cast<std::size_t>(cell)];
                if (ship < 0) {
                    h.add(cell / inst.width, cell % inst.width, Outcome::Miss);
                } else if (--remaining[static_cast<std::size_t>(ship)] == 0) {
                    h.add(cell / inst.width, cell % inst.width, Outcome::Sunk,
                          truth[static_cast<std::size_t>(ship)].length);
                } else {
                    h.add(cell / inst.width, cell % inst.width, Outcome::Hit);
                }
            }
            agree(inst, constraintsFrom(inst, h), inst.describe() + " fuzz " + std::to_string(trial));
            ++cases_run;
        }
    }
    std::printf("  %d fuzzed ordered histories, all rungs identical\n", cases_run);
}

void testFastPathLimits() {
    std::printf("[fast-path limits]\n");
    check(fastPathSupports(standardInstance()), "the standard instance fits the packed key");
    const Instance tall(4, 20, {4, 3, 2});
    check(!fastPathSupports(tall), "a 20-row board falls outside the packed key");
    std::printf("  10x10 supported, 20-row board correctly rejected\n");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testUnconstrained();
    testPinnedHistories();
    testFuzzedHistories();
    testFastPathLimits();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
