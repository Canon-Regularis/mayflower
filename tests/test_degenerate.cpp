// Degenerate fleets and boards.
//
// The ladder exercises fleets that look like Battleships, so every case it runs
// has ships of length two or more on a board with room to turn them. Four of the
// five counting sweeps were wrong on the one case nothing tried: a length-1 ship
// has one placement, and the horizontal and vertical branches both emitted it,
// so a fleet of k one-ships came back 2^k times too large. The no-touching sweep
// carried the guard from the day it was written and both brute-force oracles
// carry it with a comment, so the disagreement was there to be found and no test
// asked.
//
// Instance::validate accepts length 1, which makes those answers wrong rather
// than unsupported. The closed form is the point of the case: k indistinguishable
// single cells on n free cells is C(n, k), so the sweeps are checked against
// arithmetic and not against each other.
//
// The rest is the same idea applied to shapes: strips one cell wide, a fleet that
// exactly fills the board, a fleet that cannot fit, and a fleet mixing a one-ship
// with a real one.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/notouch.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/profile_dp_blocked.hpp"
#include "mayflower/weighted.hpp"
#include "oracle/brute_force.hpp"

namespace {

using namespace mayflower;

int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  %-58s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!detail.empty()) std::printf("      %s\n", detail.c_str());
    if (!ok) ++failures;
}

// C(n, k) in exact integers, small enough that u64 never strains.
std::uint64_t binomial(int n, int k) {
    if (k < 0 || k > n) return 0;
    k = std::min(k, n - k);
    std::uint64_t r = 1;
    for (int i = 1; i <= k; ++i) {
        r = r * static_cast<std::uint64_t>(n - k + i);
        r /= static_cast<std::uint64_t>(i);
    }
    return r;
}

// Single cells are interchangeable and never collide with anything but each
// other, so the count is a binomial coefficient and needs no enumeration.
void testSingleCellFleets() {
    std::printf("[fleets of length-1 ships, against the closed form]\n");
    int agreed = 0, trials = 0;
    bool weightedOk = true, blockedOk = true, fastOk = true;

    struct Case { int w, h, ships; };
    for (const Case& c : std::vector<Case>{{2, 2, 1}, {3, 3, 1}, {3, 3, 2}, {3, 3, 3},
                                           {4, 4, 2}, {4, 4, 5}, {5, 4, 3}, {6, 6, 4},
                                           {9, 6, 13}}) {
        const Instance inst(c.w, c.h, std::vector<int>(static_cast<std::size_t>(c.ships), 1));
        const std::uint64_t want = binomial(c.w * c.h, c.ships);
        const std::uint64_t got = countConfigurations(inst).count;
        ++trials;
        if (got == want) ++agreed;
        else
            std::printf("      %s: %llu against C(%d,%d) = %llu\n", inst.describe().c_str(),
                        static_cast<unsigned long long>(got), c.w * c.h, c.ships,
                        static_cast<unsigned long long>(want));

        // Every other free-placement sweep has to say the same thing. The fast
        // rung is here because it was the one this test did not reach, and it
        // kept the double count after the other four were fixed.
        const double wsum = weightedCount(inst, Weights::uniform()).total;
        if (wsum != static_cast<double>(want)) weightedOk = false;
        if (countConfigurationsBlocked(inst, 4).count != want) blockedOk = false;
        if (countConfigurationsFast(inst).count != want) fastOk = false;
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "%d/%d single-cell fleets match C(n,k)", agreed, trials);
    check(agreed == trials, buf);
    check(weightedOk, "the weighted sweep agrees on every one");
    check(blockedOk, "the blocked sweep agrees on every one");
    check(fastOk, "the fast rung agrees on every one");
}

// The no-touching count of k single cells is an independent set of size k in the
// king graph, which has no closed form worth writing, so it goes to the oracle.
void testSingleCellNoTouch() {
    std::printf("[length-1 ships under the no-touching rule]\n");
    int agreed = 0, trials = 0;
    struct Case { int w, h, ships; };
    for (const Case& c : std::vector<Case>{{3, 3, 1}, {3, 3, 2}, {4, 4, 2}, {4, 4, 3},
                                           {5, 4, 2}, {5, 5, 4}}) {
        const std::vector<int> fleet(static_cast<std::size_t>(c.ships), 1);
        const Instance inst(c.w, c.h, fleet);
        const std::uint64_t want = oracle::bruteForceCountNoTouch(c.w, c.h, fleet);
        const std::uint64_t got = countNoTouch(inst).count;
        ++trials;
        if (got == want) ++agreed;
        else
            std::printf("      %s: %llu against %llu enumerated\n", inst.describe().c_str(),
                        static_cast<unsigned long long>(got),
                        static_cast<unsigned long long>(want));
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "%d/%d agree with literal enumeration", agreed, trials);
    check(agreed == trials, buf);
}

// A one-ship beside a real one, where the two branches interact.
void testMixedFleets() {
    std::printf("[fleets mixing a one-ship with a real one]\n");
    int agreed = 0, trials = 0;
    struct Case { int w, h; std::vector<int> fleet; };
    for (const Case& c : std::vector<Case>{{4, 4, {2, 1}}, {4, 4, {3, 1}}, {4, 4, {2, 1, 1}},
                                           {5, 4, {3, 2, 1}}, {5, 5, {4, 1}},
                                           {5, 5, {3, 2, 1, 1}}}) {
        const Instance inst(c.w, c.h, c.fleet);
        const std::uint64_t want = oracle::bruteForceCount(c.w, c.h, c.fleet);
        const std::uint64_t got = countConfigurations(inst).count;
        ++trials;
        if (got == want &&
            weightedCount(inst, Weights::uniform()).total == static_cast<double>(want) &&
            countConfigurationsBlocked(inst, 3).count == want &&
            countConfigurationsFast(inst).count == want)
            ++agreed;
        else
            std::printf("      %s: %llu against %llu enumerated\n", inst.describe().c_str(),
                        static_cast<unsigned long long>(got),
                        static_cast<unsigned long long>(want));
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "%d/%d mixed fleets agree across all four sweeps",
                  agreed, trials);
    check(agreed == trials, buf);
}

// Shapes with no room: strips one cell wide, and fleets that exactly fill the
// board. A ship longer than both dimensions is rejected at construction rather
// than counted as zero, so that boundary is asserted rather than assumed.
void testDegenerateShapes() {
    std::printf("[boards with no room to turn]\n");
    int agreed = 0, trials = 0;
    struct Case { int w, h; std::vector<int> fleet; };
    for (const Case& c : std::vector<Case>{{1, 5, {2}}, {5, 1, {2}}, {1, 5, {3, 2}},
                                           {5, 1, {3, 2}}, {1, 1, {1}}, {2, 2, {2, 2}},
                                           {4, 1, {2, 2}}, {1, 4, {4}}, {4, 1, {4}},
                                           {2, 3, {3, 3}}, {1, 6, {2, 2, 2}}}) {
        const Instance inst(c.w, c.h, c.fleet);
        const std::uint64_t want = oracle::bruteForceCount(c.w, c.h, c.fleet);
        ++trials;
        if (countConfigurations(inst).count == want &&
            weightedCount(inst, Weights::uniform()).total == static_cast<double>(want) &&
            countConfigurationsBlocked(inst, 2).count == want)
            ++agreed;
        else
            std::printf("      %s: %llu against %llu enumerated\n", inst.describe().c_str(),
                        static_cast<unsigned long long>(countConfigurations(inst).count),
                        static_cast<unsigned long long>(want));
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "%d/%d degenerate shapes agree with enumeration",
                  agreed, trials);
    check(agreed == trials, buf);

    // A ship with nowhere to go is rejected at construction rather than counted
    // as zero, so the caller finds out before paying for a sweep.
    int rejected = 0, offered = 0;
    for (const Case& c : std::vector<Case>{{2, 2, {4}}, {3, 3, {5}}, {3, 4, {5, 4}},
                                           {1, 5, {6}}, {5, 1, {6}}}) {
        ++offered;
        try {
            const Instance inst(c.w, c.h, c.fleet);
            std::printf("      %s was accepted\n", inst.describe().c_str());
        } catch (const std::invalid_argument&) {
            ++rejected;
        }
    }
    std::snprintf(buf, sizeof buf, "%d/%d unfittable fleets rejected at construction",
                  rejected, offered);
    check(rejected == offered, buf);
}

// The exactness certificate has to bound the value it certifies, including the
// layer the last cell produces. Only a length-1 ship makes the final cell emit
// more than one edge per state, which is why the gap survived.
void testCertificateCoversTheAnswer() {
    std::printf("[the exactness certificate]\n");
    bool sound = true;
    std::string worst;
    struct Case { int w, h; std::vector<int> fleet; };
    std::vector<Case> cases{{9, 6, std::vector<int>(13, 1)}, {2, 2, {1}}, {3, 3, {1, 1, 1}},
                            {5, 5, {1, 1, 1, 1, 1}}, {10, 10, {5, 4, 3, 3, 2}}};
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        const WeightedResult r = weightedCount(inst, Weights::uniform());
        if (r.exact && r.total > r.maxLayerSum) {
            sound = false;
            worst = inst.describe() + ": total " + std::to_string(r.total) +
                    " above the certified " + std::to_string(r.maxLayerSum);
        }
    }
    check(sound, "an exact run's answer never exceeds its own certificate", worst);
}

// Instances too tall for a 64-bit no-touching key. Asking whether the key fits
// must not itself be undefined: height 20 is legal and puts the fleet field's
// shift at 84, which an unguarded `1 << shift` reaches while answering.
void testOversizedKeys() {
    std::printf("[no-touching keys wider than a word]\n");
    bool ok = true;
    struct Case { int w, h; bool fits; };
    for (const Case& c : std::vector<Case>{{10, 10, true}, {6, 10, true}, {8, 16, false},
                                           {6, 20, false}, {5, 20, false}}) {
        const Instance inst(c.w, c.h, {5, 4, 3, 3, 2});
        const int bits = noTouchKeyBits(inst);
        if ((bits <= 64) != c.fits) {
            ok = false;
            std::printf("      %s: %d bits, expected it to %s\n", inst.describe().c_str(),
                        bits, c.fits ? "fit" : "not fit");
            continue;
        }
        bool threw = false;
        try {
            (void)countNoTouch(inst);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        if (threw == c.fits) {
            ok = false;
            std::printf("      %s: %s\n", inst.describe().c_str(),
                        threw ? "refused an instance that fits" : "accepted one that does not");
        }
    }
    check(ok, "an oversized key is measured and refused, never shifted");
}

}  // namespace

int main() {
    std::printf("degenerate fleets and boards\n============================\n");
    testSingleCellFleets();
    testSingleCellNoTouch();
    testMixedFleets();
    testDegenerateShapes();
    testCertificateCoversTheAnswer();
    testOversizedKeys();
    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
