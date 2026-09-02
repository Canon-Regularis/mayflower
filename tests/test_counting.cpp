// Profile DP against the independent brute-force oracle.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
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

using mayflower::CellConstraint;
using mayflower::Instance;

void testPlacementCounts() {
    std::printf("[placement counts]\n");
    const Instance std10 = mayflower::standardInstance();
    checkEq(std10.placementsFor(5), mayflower::constants::kPlacements5, "L=5 placements");
    checkEq(std10.placementsFor(4), mayflower::constants::kPlacements4, "L=4 placements");
    checkEq(std10.placementsFor(3), mayflower::constants::kPlacements3, "L=3 placements");
    checkEq(std10.placementsFor(2), mayflower::constants::kPlacements2, "L=2 placements");

    // L = 1 included deliberately: it is the only length where the horizontal
    // and vertical branches name the same placement, so it is the only one that
    // can catch a formula that counts both.
    for (int L : {1, 2, 3, 4, 5}) {
        checkEq(static_cast<int>(oracle::placements(10, 10, L).size()),
                std10.placementsFor(L), "oracle vs formula for L=" + std::to_string(L));
    }
    checkEq(std10.shipCells(), mayflower::constants::kShipCells, "fleet cell count");
}

// Instances small enough to enumerate literally. Fleets with a repeated length
// are where a labelled-counting bug would surface.
void testDpAgainstBruteForce() {
    std::printf("[DP vs brute force]\n");
    struct Case {
        int w, h;
        std::vector<int> fleet;
    };
    const std::vector<Case> ladder = {
        {4, 4, {3, 2}},
        {5, 5, {3, 2, 2}},
        {5, 5, {4, 3, 2}},
        {6, 6, {3, 3, 2}},
        {6, 6, {4, 3, 2}},
        {5, 5, {3, 3, 2, 2}},
        {6, 6, {4, 3, 3, 2}},
        {4, 6, {3, 2}},          // non-square
        {7, 5, {4, 3, 2}},       // non-square, wide
    };
    for (const Case& c : ladder) {
        Instance inst(c.w, c.h, c.fleet);
        const auto dp = mayflower::countConfigurations(inst);
        const auto bf = oracle::bruteForceCount(c.w, c.h, c.fleet);
        checkEq(dp.count, bf, "count " + inst.describe());
        std::printf("  %-20s DP=%12llu  brute=%12llu  %s\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(dp.count),
                    static_cast<unsigned long long>(bf),
                    dp.count == bf ? "match" : "MISMATCH");
    }
}

void testMarginalsAgainstBruteForce() {
    std::printf("[marginals vs brute force]\n");
    const Instance inst(5, 5, {3, 2, 2});
    const std::uint64_t total = mayflower::countConfigurations(inst).count;
    check(total > 0, "non-zero total");

    std::uint64_t marginalSum = 0;
    for (int r = 0; r < inst.height; ++r) {
        for (int c = 0; c < inst.width; ++c) {
            const std::uint64_t dp = mayflower::occupancyCount(inst, r, c);
            const std::uint64_t bf = oracle::bruteForceOccupancy(5, 5, {3, 2, 2}, r, c);
            checkEq(dp, bf, "occupancy (" + std::to_string(r) + "," + std::to_string(c) + ")");
            marginalSum += dp;
        }
    }
    // Sum of occupancy marginals equals the ship-cell count exactly.
    checkEq(marginalSum, static_cast<std::uint64_t>(inst.shipCells()) * total,
            "sum of occupancy counts == shipCells * total");
    std::printf("  sum of marginals == %d exactly: yes\n", inst.shipCells());
}

void testConstraints() {
    std::printf("[constraints]\n");
    const Instance inst(6, 6, {4, 3, 2});
    const std::uint64_t base = mayflower::countConfigurations(inst).count;

    std::vector<CellConstraint> cells(static_cast<std::size_t>(inst.cellCount()),
                                      CellConstraint::Free);
    cells[static_cast<std::size_t>(inst.cellIndex(2, 2))] = CellConstraint::MustBeEmpty;
    const std::uint64_t afterMiss = mayflower::countConfigurations(inst, cells).count;
    check(afterMiss < base, "a miss strictly reduces the count here");

    cells[static_cast<std::size_t>(inst.cellIndex(2, 2))] = CellConstraint::MustBeOccupied;
    const std::uint64_t afterHit = mayflower::countConfigurations(inst, cells).count;
    checkEq(afterHit + afterMiss, base, "hit(c) + miss(c) == total");

    cells[static_cast<std::size_t>(inst.cellIndex(0, 0))] = CellConstraint::MustBeEmpty;
    const std::uint64_t afterBoth = mayflower::countConfigurations(inst, cells).count;
    check(afterBoth <= afterHit, "counts are monotonically non-increasing");

    std::vector<CellConstraint> allEmpty(static_cast<std::size_t>(inst.cellCount()),
                                         CellConstraint::MustBeEmpty);
    checkEq(mayflower::countConfigurations(inst, allEmpty).count, std::uint64_t{0},
            "an all-miss board is infeasible");
}

void testSymmetryOfThePrior() {
    std::printf("[D4 symmetry of the prior]\n");
    const Instance inst(6, 6, {3, 3, 2});
    const int W = inst.width, H = inst.height;
    for (int r = 0; r < H; ++r) {
        for (int c = 0; c < W; ++c) {
            const std::uint64_t base = mayflower::occupancyCount(inst, r, c);
            checkEq(mayflower::occupancyCount(inst, H - 1 - r, c), base, "vertical flip");
            checkEq(mayflower::occupancyCount(inst, r, W - 1 - c), base, "horizontal flip");
            checkEq(mayflower::occupancyCount(inst, c, r), base, "transpose (square board)");
        }
    }
}

void testIndistinguishableShips() {
    std::printf("[indistinguishable ships]\n");
    // The DP counts unordered configurations, so a repeated length must agree
    // with the oracle. A labelled-counting bug shows up as a factor of 2.
    const Instance inst(5, 5, {3, 3});
    const std::uint64_t dp = mayflower::countConfigurations(inst).count;
    const std::uint64_t bf = oracle::bruteForceCount(5, 5, {3, 3});
    checkEq(dp, bf, "5x5 {3,3}");
    check(dp * 2 != bf && bf * 2 != dp, "no factor-of-2 discrepancy in either direction");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testPlacementCounts();
    testDpAgainstBruteForce();
    testMarginalsAgainstBruteForce();
    testConstraints();
    testSymmetryOfThePrior();
    testIndistinguishableShips();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
