// marginals: the exact prior occupancy heatmap for the standard instance,
// by one forward and one backward sweep of the lattice.

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main() {
    using namespace mayflower;
    namespace k = mayflower::constants;

    const Instance inst = standardInstance();
    const int W = inst.width, H = inst.height;

    std::printf("Exact prior occupancy marginals, %s\n", inst.describe().c_str());
    std::printf("=================================================\n\n");

    const auto t0 = std::chrono::steady_clock::now();
    std::uint64_t total = 0;
    const std::vector<std::uint64_t> occ = occupancyMap(inst, total);
    const double dtForwardBackward = seconds(t0);

    std::printf("|Omega|  %llu   %s\n", static_cast<unsigned long long>(total),
                total == k::kOmega0 ? "(matches constants.hpp)" : "*** MISMATCH ***");
    std::printf("forward-backward wall time  %.3f s\n\n", dtForwardBackward);

    std::printf("      ");
    for (int c = 0; c < W; ++c) std::printf("%8d", c);
    std::printf("\n");
    for (int r = 0; r < H; ++r) {
        std::printf("  r%d  ", r);
        for (int c = 0; c < W; ++c) {
            const double p = static_cast<double>(occ[static_cast<std::size_t>(r * W + c)]) /
                             static_cast<double>(total);
            std::printf("%8.4f", p);
        }
        std::printf("\n");
    }

    // The sum of occupancy counts is shipCells * |Omega| exactly, in integers.
    std::uint64_t sum = 0;
    for (std::uint64_t v : occ) sum += v;
    const std::uint64_t want = static_cast<std::uint64_t>(inst.shipCells()) * total;
    std::printf("\nsum of counts  %llu\nexpected       %llu   %s\n",
                static_cast<unsigned long long>(sum),
                static_cast<unsigned long long>(want),
                sum == want ? "EXACT" : "*** MISMATCH ***");

    std::printf("\nD4 orbit representatives (0 <= i <= j <= 4), exact counts:\n");
    int orbits = 0;
    for (int i = 0; i < 5; ++i) {
        for (int j = i; j < 5; ++j) {
            const std::uint64_t n = occ[static_cast<std::size_t>(i * W + j)];
            std::printf("  (%d,%d)  %18llu   %.6f\n", i, j,
                        static_cast<unsigned long long>(n),
                        static_cast<double>(n) / static_cast<double>(total));
            ++orbits;
        }
    }
    std::printf("  %d orbits (constants.hpp says %d)\n", orbits, k::kD4OrbitCount);

    std::uint64_t lo = occ[0], hi = occ[0];
    for (std::uint64_t v : occ) { if (v < lo) lo = v; if (v > hi) hi = v; }
    std::printf("\ncorner %.6f, centre %.6f, ratio %.3f, mean %.6f\n",
                static_cast<double>(lo) / static_cast<double>(total),
                static_cast<double>(hi) / static_cast<double>(total),
                static_cast<double>(hi) / static_cast<double>(lo),
                static_cast<double>(sum) / static_cast<double>(total) / (W * H));

    // Cost of the same heatmap by repeated constrained counting, which is what
    // forward-backward replaces. Timed on the 15 orbit representatives only.
    std::printf("\nCross-check against constrained counting on the 15 orbits:\n");
    const auto t1 = std::chrono::steady_clock::now();
    bool agree = true;
    for (int i = 0; i < 5; ++i) {
        for (int j = i; j < 5; ++j) {
            const std::uint64_t ref = occupancyCount(inst, i, j);
            if (ref != occ[static_cast<std::size_t>(i * W + j)]) {
                agree = false;
                std::printf("  MISMATCH at (%d,%d): %llu vs %llu\n", i, j,
                            static_cast<unsigned long long>(ref),
                            static_cast<unsigned long long>(occ[static_cast<std::size_t>(i * W + j)]));
            }
        }
    }
    const double dtOrbits = seconds(t1);
    std::printf("  %s   15 constrained counts took %.3f s\n",
                agree ? "all 15 agree" : "*** DISAGREEMENT ***", dtOrbits);
    std::printf("  forward-backward gives all 100 cells in %.3f s (%.1fx faster than 15 counts,\n"
                "  and the full 100-cell version of that would cost about %.1f s)\n",
                dtForwardBackward, dtOrbits / dtForwardBackward, dtOrbits * 100.0 / 15.0);

    return (total == k::kOmega0 && sum == want && agree) ? 0 : 1;
}
