// Blocking numbers against brute force.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
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

void testAgainstBruteForce() {
    std::printf("[blocking numbers vs brute force]\n");
    struct Case { int w, h, l; };
    const std::vector<Case> cases = {
        {4, 4, 2}, {4, 4, 3}, {4, 4, 4},
        {5, 4, 2}, {5, 4, 3},
        {5, 5, 2}, {5, 5, 3}, {5, 5, 4}, {5, 5, 5},
        {6, 3, 2}, {6, 3, 3},
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

void testWitnessesAreValid() {
    std::printf("[witnesses]\n");
    for (int L : {2, 3, 4, 5}) {
        const auto witness = mayflower::blockingWitness(10, 10, L);
        check(witnessBlocksEverything(10, 10, L, witness),
              "greedy witness for L=" + std::to_string(L) + " meets every placement");
        const auto exact = mayflower::blockingNumber(10, 10, L);
        check(static_cast<int>(witness.size()) >= exact.blocking,
              "witness is no smaller than beta(L)");
        std::printf("  L=%d  beta %2d, greedy witness %2zu cells%s\n", L, exact.blocking,
                    witness.size(),
                    static_cast<int>(witness.size()) == exact.blocking ? " (optimal)" : "");
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
    testWitnessesAreValid();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
