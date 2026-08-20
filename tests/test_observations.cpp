// Observation semantics: ordered sunk handling, and forward-backward marginals.

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

using mayflower::Constraints;
using mayflower::History;
using mayflower::Instance;
using mayflower::Outcome;

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

std::uint64_t dpCount(const Instance& inst, const History& h) {
    return mayflower::countConfigurations(inst, mayflower::constraintsFrom(inst, h)).count;
}

// ---------------------------------------------------------------------------

// The engine must reproduce the oracle's ordered posterior on arbitrary
// histories. This is the test that gives sunk handling its meaning.
void testSunkAgainstOracle() {
    std::printf("[ordered posterior vs oracle]\n");
    const int W = 5, H = 5;
    const std::vector<int> fleet = {3, 2, 2};
    const Instance inst(W, H, fleet);
    const auto boards = oracle::enumerateBoards(W, H, fleet);
    checkEq(static_cast<std::uint64_t>(boards.size()), std::uint64_t{12798},
            "enumerated board count");

    Rng rng(0xC0FFEEu);
    int withSink = 0;
    const int trials = 300;
    for (int t = 0; t < trials; ++t) {
        const auto& truth = boards[static_cast<std::size_t>(rng.below(static_cast<int>(boards.size())))];

        std::vector<int> pool(static_cast<std::size_t>(W * H));
        for (int i = 0; i < W * H; ++i) pool[static_cast<std::size_t>(i)] = i;
        for (int i = W * H - 1; i > 0; --i)
            std::swap(pool[static_cast<std::size_t>(i)],
                      pool[static_cast<std::size_t>(rng.below(i + 1))]);

        const int k = 3 + rng.below(9);
        const std::vector<int> shots(pool.begin(), pool.begin() + k);
        const auto observed = oracle::simulate(truth, shots);

        History h(inst);
        for (int i = 0; i < k; ++i) {
            const int cell = shots[static_cast<std::size_t>(i)];
            const auto& o = observed[static_cast<std::size_t>(i)];
            switch (o.outcome) {
                case oracle::Outcome::Miss: h.add(cell / W, cell % W, Outcome::Miss); break;
                case oracle::Outcome::Hit:  h.add(cell / W, cell % W, Outcome::Hit);  break;
                case oracle::Outcome::Sunk:
                    h.add(cell / W, cell % W, Outcome::Sunk, o.sunkLength);
                    ++withSink;
                    break;
            }
        }

        const std::uint64_t want = oracle::posteriorCount(boards, shots, observed);
        const std::uint64_t got = dpCount(inst, h);
        checkEq(got, want, "history #" + std::to_string(t));
        check(want > 0, "the true board is always in Omega (history #" + std::to_string(t) + ")");
    }
    std::printf("  %d random histories, %d of them containing a sink\n", trials, withSink);
}

// ---------------------------------------------------------------------------

History build(const Instance& inst, const std::vector<std::tuple<int, int, Outcome, int>>& shots) {
    History h(inst);
    for (const auto& s : shots)
        h.add(std::get<0>(s), std::get<1>(s), std::get<2>(s), std::get<3>(s));
    return h;
}

// Pinned regression. Both histories use the same shot multiset and produce the
// same outcome multiset, yet their posteriors differ, so the record cannot be
// canonicalised to a set. If someone later keys a cache on {misses, hits, sunk},
// these two counts collapse to one value and this test fails.
void testOrderDependence() {
    std::printf("[order dependence, pinned]\n");
    const Instance inst(5, 5, {3, 2, 2});

    const auto orderA = build(inst, {
        {1, 1, Outcome::Hit,  0},
        {2, 1, Outcome::Hit,  0},
        {3, 3, Outcome::Miss, 0},
        {1, 2, Outcome::Hit,  0},
        {2, 0, Outcome::Sunk, 2},
        {0, 0, Outcome::Miss, 0},
        {4, 1, Outcome::Miss, 0},
    });
    const auto orderB = build(inst, {
        {1, 2, Outcome::Hit,  0},
        {1, 1, Outcome::Hit,  0},
        {4, 1, Outcome::Miss, 0},
        {3, 3, Outcome::Miss, 0},
        {2, 0, Outcome::Hit,  0},
        {0, 0, Outcome::Miss, 0},
        {2, 1, Outcome::Sunk, 2},
    });

    const std::uint64_t a = dpCount(inst, orderA);
    const std::uint64_t b = dpCount(inst, orderB);
    checkEq(a, std::uint64_t{41}, "order A posterior");
    checkEq(b, std::uint64_t{53}, "order B posterior");
    check(a != b, "set-invariance is falsified: same shots, different order, different count");
    std::printf("  order A = %llu, order B = %llu, same shot multiset\n",
                static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));

    // The history where an order-free predicate returns 26 against a true 22.
    const auto overcount = build(inst, {
        {4, 1, Outcome::Hit,  0},
        {3, 4, Outcome::Miss, 0},
        {3, 1, Outcome::Sunk, 2},
        {4, 3, Outcome::Miss, 0},
        {2, 1, Outcome::Hit,  0},
        {4, 4, Outcome::Miss, 0},
        {4, 2, Outcome::Miss, 0},
        {0, 0, Outcome::Miss, 0},
        {1, 3, Outcome::Hit,  0},
    });
    checkEq(dpCount(inst, overcount), std::uint64_t{22}, "overcount-case posterior");
}

// A sink announcement pins the ship, so it must not be weaker than the plain-hit
// record on the same cells.
void testSunkIsInformative() {
    std::printf("[sunk carries information]\n");
    const Instance inst(6, 6, {3, 2});

    History plain(inst);
    plain.add(2, 2, Outcome::Hit);
    plain.add(2, 3, Outcome::Hit);

    History sunk(inst);
    sunk.add(2, 2, Outcome::Hit);
    sunk.add(2, 3, Outcome::Sunk, 2);

    const std::uint64_t p = dpCount(inst, plain);
    const std::uint64_t s = dpCount(inst, sunk);
    check(s < p, "announcing a sink strictly narrows the posterior");
    std::printf("  two hits: %llu, same two cells with a sunk 2-ship: %llu\n",
                static_cast<unsigned long long>(p), static_cast<unsigned long long>(s));

    // An impossible announcement: a 2-ship sunk on a cell whose partner is a miss.
    History impossible(inst);
    impossible.add(2, 2, Outcome::Miss);
    impossible.add(2, 3, Outcome::Sunk, 2);
    impossible.add(1, 3, Outcome::Miss);
    impossible.add(3, 3, Outcome::Miss);
    impossible.add(2, 4, Outcome::Miss);
    checkEq(dpCount(inst, impossible), std::uint64_t{0},
            "a sink with no legal ship is infeasible");
}

// ---------------------------------------------------------------------------

void testForwardBackwardMarginals() {
    std::printf("[forward-backward marginals]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {
        {4, 4, {3, 2}}, {5, 5, {3, 2, 2}}, {6, 6, {4, 3, 2}}, {4, 6, {3, 2}},
    };
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        std::uint64_t total = 0;
        const auto map = mayflower::occupancyMap(inst, total);
        checkEq(total, mayflower::countConfigurations(inst).count, "total from occupancyMap");

        std::uint64_t sum = 0;
        for (int r = 0; r < inst.height; ++r) {
            for (int cc = 0; cc < inst.width; ++cc) {
                const std::uint64_t want = mayflower::occupancyCount(inst, r, cc);
                const std::uint64_t got = map[static_cast<std::size_t>(r * inst.width + cc)];
                checkEq(got, want, inst.describe() + " cell (" + std::to_string(r) + "," +
                                       std::to_string(cc) + ")");
                sum += got;
            }
        }
        checkEq(sum, static_cast<std::uint64_t>(inst.shipCells()) * total,
                inst.describe() + " marginals sum to shipCells * total");
        std::printf("  %-14s all %d cells match the reference, sum == %d * total\n",
                    inst.describe().c_str(), inst.cellCount(), inst.shipCells());
    }
}

// Marginals must stay exact once observations are applied, including sinks.
void testMarginalsUnderObservations() {
    std::printf("[marginals under observations]\n");
    const Instance inst(5, 5, {3, 2, 2});
    History h(inst);
    h.add(1, 1, Outcome::Hit);
    h.add(0, 3, Outcome::Miss);
    h.add(2, 1, Outcome::Hit);
    h.add(3, 1, Outcome::Sunk, 3);
    h.add(4, 4, Outcome::Miss);

    const Constraints c = mayflower::constraintsFrom(inst, h);
    std::uint64_t total = 0;
    const auto map = mayflower::occupancyMap(inst, c, total);
    checkEq(total, mayflower::countConfigurations(inst, c).count, "total under observations");
    check(total > 0, "history is feasible");

    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < map.size(); ++i) sum += map[i];
    checkEq(sum, static_cast<std::uint64_t>(inst.shipCells()) * total,
            "marginals still sum to shipCells * total");

    // Hits are certain, misses are impossible.
    checkEq(map[static_cast<std::size_t>(1 * 5 + 1)], total, "marginal at a hit equals total");
    checkEq(map[static_cast<std::size_t>(3 * 5 + 1)], total, "marginal at a sunk cell equals total");
    checkEq(map[static_cast<std::size_t>(0 * 5 + 3)], std::uint64_t{0}, "marginal at a miss is zero");
    checkEq(map[static_cast<std::size_t>(4 * 5 + 4)], std::uint64_t{0}, "marginal at a miss is zero");
    std::printf("  |Omega| = %llu after 5 shots; hits at 1.0, misses at 0.0\n",
                static_cast<unsigned long long>(total));
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testSunkAgainstOracle();
    testOrderDependence();
    testSunkIsInformative();
    testForwardBackwardMarginals();
    testMarginalsUnderObservations();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
