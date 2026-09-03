// Blocking numbers against brute force.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "mayflower/certify.hpp"
#include "mayflower/constants.hpp"

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
        std::printf("  FAIL  %s: got %lld, want %lld\n", what.c_str(),
                    static_cast<long long>(got), static_cast<long long>(want));
    }
}

// Exhaustive: the largest cell set with no L consecutive cells in any line.
int bruteForceFreeSet(int W, int H, int L) {
    const int n = W * H;
    int best = 0;
    for (std::uint32_t mask = 0; mask < (1u << n); ++mask) {
        bool ok = true;
        for (int r = 0; r < H && ok; ++r)
            for (int c = 0; c + L <= W && ok; ++c) {
                int run = 0;
                for (int k = 0; k < L; ++k)
                    run += static_cast<int>((mask >> static_cast<unsigned>(r * W + c + k)) & 1u);
                if (run == L) ok = false;
            }
        if (L > 1)
            for (int c = 0; c < W && ok; ++c)
                for (int r = 0; r + L <= H && ok; ++r) {
                    int run = 0;
                    for (int k = 0; k < L; ++k)
                        run += static_cast<int>((mask >> static_cast<unsigned>((r + k) * W + c)) & 1u);
                    if (run == L) ok = false;
                }
        if (ok) best = std::max(best, static_cast<int>(__builtin_popcount(mask)));
    }
    return best;
}

bool witnessBlocksEverything(int W, int H, int L, const std::vector<int>& cells) {
    std::vector<char> hit(static_cast<std::size_t>(W * H), 0);
    for (int c : cells) hit[static_cast<std::size_t>(c)] = 1;
    for (int r = 0; r < H; ++r)
        for (int c = 0; c + L <= W; ++c) {
            bool met = false;
            for (int k = 0; k < L; ++k) met = met || hit[static_cast<std::size_t>(r * W + c + k)];
            if (!met) return false;
        }
    if (L > 1)
        for (int c = 0; c < W; ++c)
            for (int r = 0; r + L <= H; ++r) {
                bool met = false;
                for (int k = 0; k < L; ++k) met = met || hit[static_cast<std::size_t>((r + k) * W + c)];
                if (!met) return false;
            }
    return true;
}

// Enumerate every assignment of hits to ships, build the announcement string,
// and count the distinct results. Several assignments collapse to one string,
// which is exactly what the subset construction has to get right.
std::uint64_t bruteForceTranscripts(std::vector<int> fleet) {
    std::sort(fleet.begin(), fleet.end(), std::greater<int>());
    const int total = std::accumulate(fleet.begin(), fleet.end(), 0);
    std::set<std::string> strings;
    std::vector<int> hits(fleet.size(), 0);
    std::string current;

    const std::function<void(int)> go = [&](int placed) {
        if (placed == total) { strings.insert(current); return; }
        for (std::size_t i = 0; i < fleet.size(); ++i) {
            if (hits[i] >= fleet[i]) continue;
            ++hits[i];
            const bool sinks = hits[i] == fleet[i];
            const std::string symbol = sinks ? ("S" + std::to_string(fleet[i])) : "H";
            const std::size_t mark = current.size();
            current += symbol;
            current += ".";
            go(placed + 1);
            current.resize(mark);
            --hits[i];
        }
    };
    go(0);
    return strings.size();
}

void testTranscriptCount() {
    std::printf("[hit transcripts vs brute force]\n");
    const std::vector<std::vector<int>> fleets = {
        {2}, {2, 2}, {3, 2}, {3, 3}, {3, 3, 2}, {4, 3, 2}, {4, 3, 3, 2}, {2, 2, 2},
        {1}, {1, 1}, {2, 1}, {1, 1, 1}, {3, 2, 1},
    };
    for (const auto& fleet : fleets) {
        const std::uint64_t got = mayflower::countHitTranscripts(fleet);
        const std::uint64_t want = bruteForceTranscripts(fleet);
        std::string label;
        for (std::size_t i = 0; i < fleet.size(); ++i) {
            if (i) label += ",";
            label += std::to_string(fleet[i]);
        }
        checkEq(got, want, "{" + label + "} transcript count");
        std::printf("  {%-10s}  K = %6llu  (brute force agrees)\n", label.c_str(),
                    static_cast<unsigned long long>(got));
    }
}

void testWaterFillingIsSound() {
    std::printf("[water-filling]\n");
    // On a tiny instance the bound must not exceed the achievable optimum, and it
    // must never fall below the coverage bound minus rounding.
    const std::vector<int> fleet = {5, 4, 3, 3, 2};
    const auto wf = mayflower::waterFillingBound(fleet, mayflower::constants::kOmega0, 100);
    check(wf.bound >= 0.0, "bound is non-negative");
    check(wf.bound <= 100.0, "bound cannot exceed the board size");
    check(wf.shipCells == 17, "ship-cell count");
    std::printf("  K = %llu, bound = %.4f shots, saturates at depth %d\n",
                static_cast<unsigned long long>(wf.hitTranscripts), wf.bound, wf.saturatesAt);
}

void testAgainstBruteForce() {
    std::printf("[blocking numbers vs brute force]\n");
    struct Case { int w, h, l; };
    const std::vector<Case> cases = {
        {4, 4, 1}, {4, 4, 2}, {4, 4, 3}, {4, 4, 4},
        {5, 4, 1}, {5, 4, 2}, {5, 4, 3},
        {5, 5, 2}, {5, 5, 3}, {5, 5, 4}, {5, 5, 5},
        {6, 3, 1}, {6, 3, 2}, {6, 3, 3},
    };
    for (const Case& c : cases) {
        const auto dp = mayflower::blockingNumber(c.w, c.h, c.l);
        const int brute = bruteForceFreeSet(c.w, c.h, c.l);
        checkEq(dp.largestFreeSet, brute,
                std::to_string(c.w) + "x" + std::to_string(c.h) + " L=" + std::to_string(c.l) +
                    " largest free set");
        checkEq(dp.blocking, c.w * c.h - brute, "beta complements the free set");
        std::printf("  %dx%d L=%d  free %2d  beta %2d  (brute force agrees)\n", c.w, c.h, c.l,
                    dp.largestFreeSet, dp.blocking);
    }
}

// The survival term divides by the hypothesis count, so a fleet that cannot be
// placed produced 1 - x/0 at every depth and the bound came back NaN. A NaN
// bound is worse than no bound: it compares false against every comparison a
// caller might make, so the rung is silently discarded rather than reported.
void testWaterFillingRefusesAnEmptySpace() {
    std::printf("[water filling on an empty space]\n");
    ++gChecks;
    try {
        const auto r = mayflower::waterFillingBound({2, 2, 2}, 0, 4);
        ++gFailures;
        std::printf("  FAIL  returned %.4f for a space with no configurations\n", r.bound);
    } catch (const std::invalid_argument&) {
        std::printf("  an empty hypothesis space is refused\n");
    }

    // And a real space still produces a finite bound, so the guard has not
    // turned the working case into a refusal.
    ++gChecks;
    const auto ok = mayflower::waterFillingBound({5, 4, 3, 3, 2},
                                                 mayflower::constants::kOmega0, 100);
    if (!(ok.bound > 0.0) || ok.bound != ok.bound) {
        ++gFailures;
        std::printf("  FAIL  the standard instance no longer bounds: %.4f\n", ok.bound);
    } else {
        std::printf("  the standard instance still bounds at %.4f shots\n", ok.bound);
    }
}

void testWitnessesAreValid() {
    std::printf("[witnesses]\n");
    for (int L : {1, 2, 3, 4, 5}) {
        const auto found = mayflower::blockingWitness(10, 10, L);
        const std::vector<int>& witness = found.cells;
        check(witnessBlocksEverything(10, 10, L, witness),
              "witness for L=" + std::to_string(L) + " meets every placement");
        const auto exact = mayflower::blockingNumber(10, 10, L);
        check(static_cast<int>(witness.size()) >= exact.blocking,
              "witness is no smaller than beta(L)");
        // The flag has to describe the set rather than the intention behind it,
        // since the figure labels the drawing from it.
        check(found.optimal == (static_cast<int>(witness.size()) == exact.blocking),
              "the optimal flag matches the set it came with");
        std::printf("  L=%d  beta %2d, witness %2zu cells%s%s\n", L, exact.blocking,
                    witness.size(), found.optimal ? " (minimum)" : " (greedy, not minimum)",
                    found.selfReduced ? ", self-reduced" : "");
    }
}

// The classic parity result: a length-2 ship covers one cell of each diagonal
// colour class, so half the board blocks every domino and no smaller set does.
void testParityCase() {
    std::printf("[parity]\n");
    const auto b = mayflower::blockingNumber(10, 10, 2);
    checkEq(b.blocking, 50, "beta(2) on 10x10 is half the board");
    checkEq(b.largestFreeSet, 50, "the largest domino-free set is the checkerboard");
    std::printf("  beta(2) = 50, matching the checkerboard argument\n");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testAgainstBruteForce();
    testParityCase();
    testWaterFillingRefusesAnEmptySpace();
    testWitnessesAreValid();
    testTranscriptCount();
    testWaterFillingIsSound();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
