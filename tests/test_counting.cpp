// Profile DP against the independent brute-force oracle.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/constraints.hpp"
#include "mayflower/counting.hpp"
#include "mayflower/flows.hpp"
// For the rung agreement on CountResult.exact. The overflow flag is part of the
// ladder's contract, so it is checked across the ladder rather than on V0 alone.
#include "mayflower/profile_dp_blocked.hpp"
#include "mayflower/platform.hpp"

#include "harness.hpp"
#include "oracle/brute_force.hpp"

namespace {

using mf::test::expect;
using mf::test::gChecks;
using mf::test::gFailures;
using mf::test::checkEq;

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

// validate() is the only thing standing between a caller and a board the rest of
// the engine cannot represent, so the cases it must refuse are worth pinning.
void testValidationRefusesBadInstances() {
    std::printf("[instance validation]\n");
    struct Case { int w, h; std::vector<int> fleet; const char* why; };
    const std::vector<Case> bad = {
        {0, 10, {2}, "zero width"},
        {10, 0, {2}, "zero height"},
        {-4, 4, {2}, "negative width"},
        {20, 20, {2}, "400 cells, past the 128 bound"},
        // The product wraps in a signed int: 200000000 * 20 is -294967296,
        // which slipped under the bound and left cellCount() negative.
        {200000000, 20, {2}, "a cell count that overflows a 32-bit product"},
        {10, 10, {}, "empty fleet"},
        {10, 10, {0}, "a zero-length ship"},
        {10, 10, {-2}, "a negative-length ship"},
        {4, 4, {9}, "a ship longer than either side"},
        {10, 30, {2}, "height past what the profile packs"},
        // A length-9 ship on a 4x4 is refused for not fitting the board,
        // which is a different guard entirely. This one fits the board, so
        // it reaches the three-bit residual bound, which no case above did.
        {10, 10, {9}, "a length-9 ship on a board wide enough to hold it"},
        {43, 3, {2}, "129 cells, one past the bound"},
        {6, 21, {2}, "21 rows, one past what the profile packs"},
    };
    for (const Case& c : bad) {
        ++gChecks;
        try {
            const mayflower::Instance inst(c.w, c.h, c.fleet);
            ++gFailures;
            std::printf("  FAIL  %s was accepted (%s, cellCount %d)\n", c.why,
                        inst.describe().c_str(), inst.cellCount());
        } catch (const std::invalid_argument&) {
            std::printf("  refused: %s\n", c.why);
        }
    }

    // Every refusal sits one step past its limit, and these sit exactly on
    // it. Without both sides a bound that is off by one refuses work it
    // should accept and no test notices. Length 8 is the case that matters
    // most: the sweeps pack a residual of maxLen - 1 into three bits, so 8
    // is the largest length those three bits hold.
    struct Ok { int w, h; std::vector<int> fleet; const char* why; };
    const std::vector<Ok> atTheLimit = {
        {16, 8, {2}, "128 cells, exactly the bound"},
        {6, 20, {2}, "20 rows, exactly what the profile packs"},
        {10, 10, {8}, "a length-8 ship, the widest three bits hold"},
    };
    for (const Ok& c : atTheLimit) {
        ++gChecks;
        try {
            const mayflower::Instance inst(c.w, c.h, c.fleet);
            std::printf("  accepted: %-48s %s\n", c.why,
                        inst.describe().c_str());
        } catch (const std::invalid_argument& e) {
            ++gFailures;
            std::printf("  FAIL  %s was refused (%s)\n", c.why, e.what());
        }
    }

    // And a legal instance still constructs, so the guard is not just refusing
    // everything.
    ++gChecks;
    const mayflower::Instance ok(10, 10, {5, 4, 3, 3, 2});
    if (ok.cellCount() != 100 || ok.shipCells() != 17) {
        ++gFailures;
        std::printf("  FAIL  the standard instance no longer validates\n");
    } else {
        std::printf("  and 10x10 {5,4,3,3,2} still validates\n");
    }
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
    expect(total > 0, "non-zero total");

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

    std::vector<CellConstraint> cells = freeConstraints(inst).cells;
    cells[static_cast<std::size_t>(inst.cellIndex(2, 2))] = CellConstraint::MustBeEmpty;
    const std::uint64_t afterMiss = mayflower::countConfigurations(inst, cells).count;
    expect(afterMiss < base, "a miss strictly reduces the count here");

    cells[static_cast<std::size_t>(inst.cellIndex(2, 2))] = CellConstraint::MustBeOccupied;
    const std::uint64_t afterHit = mayflower::countConfigurations(inst, cells).count;
    checkEq(afterHit + afterMiss, base, "hit(c) + miss(c) == total");

    cells[static_cast<std::size_t>(inst.cellIndex(0, 0))] = CellConstraint::MustBeEmpty;
    const std::uint64_t afterBoth = mayflower::countConfigurations(inst, cells).count;
    expect(afterBoth <= afterHit, "counts are monotonically non-increasing");

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

void testCountOverflowIsReported() {
    std::printf("[counting past 64 bits]\n");
    // A sweep that wraps used to return a plausible nineteen-digit answer with
    // nothing to say it had. The counting path is unsigned, so it does not trap
    // and -ftrapv does nothing for it; weightedCount reported this class through
    // maxLayerSum and the integer path, which is the one that publishes
    // 15,046,987,768, had no equivalent.
    //
    // Pinned from both sides, because a guard that has never been the reason for
    // a refusal proves nothing. 16x8 with fifteen 1-ships is C(128,15), which
    // fits; adding one more ship makes it C(128,16) = 9.334e19, which does not.
    // Both instances pass Instance::validate() on every clause.
    const Instance fits(16, 8, std::vector<int>(15, 1));
    const Instance over(16, 8, std::vector<int>(16, 1));
    fits.validate();
    over.validate();

    const auto a = mayflower::countConfigurations(fits);
    checkEq(a.count, std::uint64_t{13216710966550396800ull}, "16x8 with fifteen 1-ships is C(128,15)");
    expect(a.exact, "and it is reported exact");

    const auto b = mayflower::countConfigurations(over);
    expect(!b.exact, "16x8 with sixteen 1-ships is refused as inexact");
    // The wrapped value is still returned, deliberately: the flag is the answer
    // to whether it can be trusted, and a caller that ignores the flag should
    // see the same number it always saw rather than a different silent one.
    checkEq(b.count, std::uint64_t{1109300832714419320ull},
            "and the value returned is still C(128,16) modulo 2^64");

    // The instance every published number comes from is nowhere near the edge.
    const auto standard = mayflower::countConfigurations(mayflower::standardInstance());
    checkEq(standard.count, std::uint64_t{15046987768ull}, "10x10 {5,4,3,3,2} is unchanged");
    expect(standard.exact, "and exact, with 30 bits of headroom");

    // Every rung reports it, not just the reference. CountResult.exact is part
    // of the ladder's contract now, and a flag only V0 computes would be worse
    // than no flag: a caller on the fast path would read exact = true from a
    // default-initialised field and take it for an answer.
    if (mayflower::fastPathSupports(over)) {
        const auto fast = mayflower::countConfigurationsFast(over);
        checkEq(fast.count, b.count, "V1 returns the same wrapped value as V0");
        expect(!fast.exact, "and refuses it as inexact as well");
    }
    if (mayflower::blockedPathSupports(over)) {
        const auto blocked = mayflower::countConfigurationsBlocked(over);
        checkEq(blocked.count, b.count, "V2 returns the same wrapped value as V0");
        expect(!blocked.exact, "and refuses it as inexact as well");
    }
    // And the rungs agree on the instance that fits, where all four must say so.
    if (mayflower::fastPathSupports(fits))
        expect(mayflower::countConfigurationsFast(fits).exact,
               "V1 calls the fifteen-ship case exact, as V0 does");
    if (mayflower::blockedPathSupports(fits))
        expect(mayflower::countConfigurationsBlocked(fits).exact,
               "V2 calls the fifteen-ship case exact, as V0 does");
}

void testIndistinguishableShips() {
    std::printf("[indistinguishable ships]\n");
    // The DP counts unordered configurations, so a repeated length must agree
    // with the oracle. A labelled-counting bug shows up as a factor of 2.
    const Instance inst(5, 5, {3, 3});
    const std::uint64_t dp = mayflower::countConfigurations(inst).count;
    const std::uint64_t bf = oracle::bruteForceCount(5, 5, {3, 3});
    checkEq(dp, bf, "5x5 {3,3}");
    expect(dp * 2 != bf && bf * 2 != dp, "no factor-of-2 discrepancy in either direction");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testPlacementCounts();

    testValidationRefusesBadInstances();
    testDpAgainstBruteForce();
    testMarginalsAgainstBruteForce();
    testConstraints();
    testSymmetryOfThePrior();
    testIndistinguishableShips();
    testCountOverflowIsReported();

    const auto dt = mf::test::elapsed(t0);
    return mf::test::report(dt);
}
