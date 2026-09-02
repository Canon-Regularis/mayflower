// The transfer matrix against brute force and against its own finite patches.

#include <chrono>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "mayflower/spectrum.hpp"

namespace {

int gFailures = 0, gChecks = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFailures; std::printf("  FAIL  %s\n", what.c_str()); }
}

// Every packing of an H x W patch with non-overlapping k-mers, counted by
// literal enumeration over cell subsets. Independent of the transfer sweep.
double bruteForcePartition(int H, int W, int k, double z) {
    const int n = H * W;
    if (n > 22) return -1;
    std::vector<std::uint32_t> rods;
    for (int r = 0; r < H; ++r)
        for (int c = 0; c + k <= W; ++c) {
            std::uint32_t m = 0;
            for (int t = 0; t < k; ++t) m |= 1u << (r * W + c + t);
            rods.push_back(m);
        }
    if (k > 1)
        for (int c = 0; c < W; ++c)
            for (int r = 0; r + k <= H; ++r) {
                std::uint32_t m = 0;
                for (int t = 0; t < k; ++t) m |= 1u << ((r + t) * W + c);
                rods.push_back(m);
            }

    // Sum z^(rods used) over every set of pairwise-disjoint rods.
    double total = 0;
    const std::size_t R = rods.size();
    std::vector<int> stack;
    const std::function<void(std::size_t, std::uint32_t, int)> go =
        [&](std::size_t i, std::uint32_t used, int count) {
            if (i == R) { total += std::pow(z, count); return; }
            go(i + 1, used, count);                       // skip this rod
            if ((used & rods[i]) == 0) go(i + 1, used | rods[i], count + 1);
        };
    go(0, 0u, 0);
    return total;
}

void testAgainstBruteForce() {
    std::printf("[partition function vs brute force]\n");
    struct C { int H, W, k; double z; };
    for (const C& c : std::vector<C>{{2,3,2,1.0},{3,3,2,1.0},{3,3,2,0.5},{2,4,2,1.0},
                                     {3,3,3,1.0},{4,4,2,1.0},{4,4,3,0.7},{3,4,4,1.0}}) {
        const double dp = mayflower::partitionFunction(c.H, c.W, c.k, c.z);
        const double bf = bruteForcePartition(c.H, c.W, c.k, c.z);
        const bool ok = bf >= 0 && std::abs(dp - bf) <= 1e-6 * std::max(1.0, bf);
        check(ok, std::to_string(c.H) + "x" + std::to_string(c.W) + " k=" +
                      std::to_string(c.k) + " z=" + std::to_string(c.z));
        std::printf("  %dx%d k=%d z=%.1f   sweep %.6g   brute %.6g   %s\n", c.H, c.W, c.k,
                    c.z, dp, bf, ok ? "match" : "MISMATCH");
    }
}

// A known value: with k=2 and z=1 the count of all ways to lay non-overlapping
// dominoes on a 1 x W strip, empty cells allowed, is the Fibonacci sequence.
// Monomers are the one rod length where the two orientations describe the same
// placement. Every cell is then independently empty or filled, so Z is exactly
// (1+z)^(H*W); a sweep that offered a vertical monomer as well would return
// (1+2z)^(H*W). The closed form makes this exact rather than a sanity check.
void testMonomerClosedForm() {
    std::printf("[monomers have one orientation]\n");
    double worst = 0.0;
    for (int H = 1; H <= 4; ++H) {
        for (int W = 1; W <= 4; ++W) {
            for (double z : {0.25, 1.0, 3.0}) {
                const double got = mayflower::partitionFunction(H, W, 1, z);
                const double want = std::pow(1.0 + z, H * W);
                worst = std::max(worst, std::abs(got - want) / want);
            }
        }
    }
    check(worst < 1e-9, "Z for monomers is (1+z)^(H*W) on every strip tried");
    std::printf("  largest relative departure %.3e over 48 cases\n", worst);

    // lambda is the growth per column, not per site, so a height-H strip of
    // independent cells grows as (1+z)^H.
    for (int H = 1; H <= 4; ++H) {
        const auto sp = mayflower::transferSpectrum(H, 1, 1.0);
        const double want = std::pow(2.0, H);
        check(std::abs(sp.lambdaMax - want) < 1e-8,
              "lambda_max for monomers on a height-" + std::to_string(H) +
                  " strip is 2^" + std::to_string(H));
    }
}

void testFibonacciStrip() {
    std::printf("[1-row strip is Fibonacci]\n");
    double a = 1, b = 1;   // F(1)=1, F(2)=1 with Z(0)=1, Z(1)=1
    for (int W = 1; W <= 12; ++W) {
        const double z = mayflower::partitionFunction(1, W, 2, 1.0);
        check(std::abs(z - b) < 1e-9, "Z(1x" + std::to_string(W) + ") = " + std::to_string(b));
        const double nxt = a + b; a = b; b = nxt;
    }
    std::printf("  Z(1xW) for W=1..12 follows 1,2,3,5,8,... exactly\n");
    // and the growth rate is the golden ratio
    const auto s = mayflower::transferSpectrum(1, 2, 1.0);
    const double phi = (1 + std::sqrt(5.0)) / 2;
    check(std::abs(s.lambdaMax - phi) < 1e-8,
          "growth rate of the 1-row dimer strip is the golden ratio");
    std::printf("  lambda_max = %.10f, golden ratio %.10f\n", s.lambdaMax, phi);
}

// Z(W+1)/Z(W) has to approach the eigenvalue the power method reports. It gets
// there from alternating sides, since the subdominant eigenvalue is negative for
// these strips, and longer rods need more columns before it settles.
void testGrowthMatchesRatio() {
    std::printf("[eigenvalue vs finite-patch ratio]\n");
    struct C { int H, k; };
    for (const C& c : std::vector<C>{{2,2},{3,2},{4,2},{3,3},{4,3},{5,3},{4,4}}) {
        const auto s = mayflower::transferSpectrum(c.H, c.k, 1.0);
        const double z1 = mayflower::partitionFunction(c.H, 48, c.k, 1.0);
        const double z2 = mayflower::partitionFunction(c.H, 49, c.k, 1.0);
        const double ratio = z2 / z1;
        const bool ok = std::abs(ratio - s.lambdaMax) <= 1e-7 * s.lambdaMax;
        check(ok, "H=" + std::to_string(c.H) + " k=" + std::to_string(c.k) + " ratio matches");
        std::printf("  H=%d k=%d   lambda %.10f   Z(49)/Z(48) %.10f   xi %.2f cols%s  %s\n",
                    c.H, c.k, s.lambdaMax, ratio, s.correlationLength,
                    s.alternating ? ", alternating" : "", ok ? "" : "MISMATCH");
    }
}

// The strip entropy converges to the two-dimensional value like f(H) = f - a/H.
// Cancelling that correction should land on the monomer-dimer entropy of the
// square lattice, which is a constant this code was never told.
void testDimerEntropyLimit() {
    std::printf("[two-dimensional dimer entropy]\n");
    double previous = 0, extrapolated = 0;
    for (int h = 2; h <= 12; ++h) {
        const auto s = mayflower::transferSpectrum(h, 2, 1.0);
        if (h > 2) extrapolated = h * s.freeEnergyPerSite - (h - 1) * previous;
        previous = s.freeEnergyPerSite;
    }
    check(std::abs(extrapolated - 0.6627989727) < 2e-5,
          "extrapolated dimer entropy matches the monomer-dimer constant");
    std::printf("  extrapolated %.7f, published 0.6627989727, difference %.1e\n",
                extrapolated, std::abs(extrapolated - 0.6627989727));
}

void testMonotonicity() {
    std::printf("[shape checks]\n");
    // More fugacity packs more rods in.
    double last = -1;
    for (double z : {0.25, 0.5, 1.0, 2.0, 4.0}) {
        const auto s = mayflower::transferSpectrum(5, 3, z);
        check(s.lambdaMax > last, "lambda increases with fugacity at z=" + std::to_string(z));
        check(s.density >= 0 && s.density <= 1.0001, "density stays in [0,1]");
        last = s.lambdaMax;
    }
    // Longer rods pack a strip less freely at the same fugacity.
    const auto k2 = mayflower::transferSpectrum(6, 2, 1.0);
    const auto k4 = mayflower::transferSpectrum(6, 4, 1.0);
    check(k2.lambdaMax > k4.lambdaMax, "dimers beat 4-mers for packings at z=1");
    std::printf("  H=6 z=1: k=2 lambda %.4f density %.3f, k=4 lambda %.4f density %.3f\n",
                k2.lambdaMax, k2.density, k4.lambdaMax, k4.density);
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    testAgainstBruteForce();
    testMonomerClosedForm();
    testFibonacciStrip();
    testGrowthMatchesRatio();
    testDimerEntropyLimit();
    testMonotonicity();
    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
