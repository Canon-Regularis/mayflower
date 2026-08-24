// omega0: reproduce the size of the hypothesis space and report lattice statistics.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/notouch.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void reportInstance(const mayflower::Instance& inst, const char* label) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = mayflower::countConfigurations(inst);
    const double dt = seconds(t0);

    std::printf("%-28s %s\n", label, inst.describe().c_str());
    std::printf("  |Omega|        %20llu\n", static_cast<unsigned long long>(r.count));
    std::printf("  log2|Omega|    %20.4f bits\n",
                r.count ? std::log2(static_cast<double>(r.count)) : 0.0);
    std::printf("  peak states    %20llu\n", static_cast<unsigned long long>(r.peakStates));
    std::printf("  state visits   %20llu\n", static_cast<unsigned long long>(r.stateVisits));
    std::printf("  edges relaxed  %20llu\n", static_cast<unsigned long long>(r.edges));
    std::printf("  wall time      %20.3f s   (%.1f M edges/s)\n", dt,
                dt > 0 ? static_cast<double>(r.edges) / dt / 1e6 : 0.0);
    std::printf("\n");
}

}  // namespace

int main() {
    using namespace mayflower;
    namespace k = mayflower::constants;

    std::printf("Mayflower: exact model counting over fleet configurations\n");
    std::printf("=========================================================\n\n");

    std::printf("Placements per ship length on %dx%d (2N(N-L+1)):\n",
                k::kBoardWidth, k::kBoardHeight);
    const Instance std10 = standardInstance();
    for (int L : {5, 4, 3, 2})
        std::printf("  L=%d  %4d\n", L, std10.placementsFor(L));
    std::printf("  distinct placement masks: %d\n\n", k::kDistinctPlacements);

    reportInstance(std10, "STANDARD INSTANCE");

    const auto r = countConfigurations(std10);
    const bool match = r.count == k::kOmega0;
    std::printf("Expected  %llu\n", static_cast<unsigned long long>(k::kOmega0));
    std::printf("Computed  %llu\n", static_cast<unsigned long long>(r.count));
    std::printf("VERDICT   %s\n\n", match ? "MATCH" : "*** MISMATCH ***");

    if (match) {
        std::printf("Derived quantities:\n");
        std::printf("  labelled count (3-ships distinct)  %llu\n",
                    static_cast<unsigned long long>(k::kOmega0Labelled));
        std::printf("  H(Omega_0)                         %.4f bits\n",
                    std::log2(static_cast<double>(r.count)));
        std::printf("  coverage bound  E1                 %d shots\n", k::kCoverageBound);
        std::printf("  entropy  bound  E2                 %.2f shots  (log2|Omega| / log2 6)\n",
                    std::log2(static_cast<double>(r.count)) / k::kMaxBitsPerShot);
        std::printf("  E2 < E1, so coverage is the binding constraint.\n");
        std::printf("  largest accumulator 17*|Omega|     %llu  (%.2f bits, %d spare in u64)\n",
                    static_cast<unsigned long long>(k::kMaxAccumulator),
                    std::log2(static_cast<double>(k::kMaxAccumulator)),
                    64 - static_cast<int>(std::ceil(std::log2(static_cast<double>(k::kMaxAccumulator)))));
        std::printf("\n");
    }

    // The printed-puzzle ruleset on the same board and fleet. Distinct ships may
    // not share an edge or a corner, which needs the previous column's occupancy
    // in the boundary state. Both counts come from the same sweep skeleton.
    {
        const auto t0 = std::chrono::steady_clock::now();
        const mayflower::CountResult nt = mayflower::countNoTouch(std10);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const bool ok = nt.count == k::kOmegaNoTouch;

        std::printf("Ships may not touch (the printed puzzle rule):\n");
        std::printf("  Expected  %llu\n", static_cast<unsigned long long>(k::kOmegaNoTouch));
        std::printf("  Computed  %llu\n", static_cast<unsigned long long>(nt.count));
        std::printf("  VERDICT   %s\n", ok ? "MATCH" : "*** MISMATCH ***");
        std::printf("  %.3f s, peak %zu states, %llu edges, %d-bit key\n", seconds,
                    nt.peakStates, static_cast<unsigned long long>(nt.edges),
                    mayflower::noTouchKeyBits(std10));
        std::printf("  H                                  %.4f bits\n",
                    std::log2(static_cast<double>(nt.count)));
        std::printf("  share of the touching count        %.4f\n",
                    static_cast<double>(nt.count) / static_cast<double>(r.count));
        std::printf("  Forbidding contact removes %.2f%% of the space and %.1f%% of the\n",
                    100.0 * (1.0 - static_cast<double>(nt.count) / static_cast<double>(r.count)),
                    100.0 * (1.0 - static_cast<double>(nt.edges) /
                                       static_cast<double>(r.edges)));
        std::printf("  lattice. The boundary state gained H+1 bits and the sweep still got\n");
        std::printf("  cheaper, because the adjacency rule kills more profiles than the extra\n");
        std::printf("  bits create.\n");
        std::printf("\n");
    }

    std::printf("Same fleet, varying board size (same DP, no enumeration):\n");
    for (int n : {6, 7, 8, 9, 10, 11, 12}) {
        try {
            Instance inst(n, n, {5, 4, 3, 3, 2});
            const auto t0 = std::chrono::steady_clock::now();
            const auto res = countConfigurations(inst);
            std::printf("  %2dx%-2d  |Omega| = %18llu   (%6.3f s, peak %7llu states)\n", n, n,
                        static_cast<unsigned long long>(res.count), seconds(t0),
                        static_cast<unsigned long long>(res.peakStates));
        } catch (const std::exception& e) {
            std::printf("  %2dx%-2d  skipped: %s\n", n, n, e.what());
        }
    }

    return match ? 0 : 1;
}
