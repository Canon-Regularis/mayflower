// The constraint density sweep.
//
// Both engines decide the same records. The backtracking search shows the
// easy-hard-easy curve; the DP does not, because a counting sweep never
// backtracks and a constraint removes work rather than adding it.

#include "core.hpp"

namespace mayflower::m9 {

void sweepDensity(const Instance& inst, int shots, int maxHits, int step, int samples) {
    std::printf("  %s, %d cells shot, %d records per point\n", inst.describe().c_str(),
                shots, samples);
    std::printf("  %6s %10s %14s %11s %12s %12s %10s\n", "hits", "feasible",
                "median |Omega|", "DP mean us", "DP states", "search nodes", "capped");

    int dpPeakAt = 0, btPeakAt = 0;
    double dpPeak = 0, btPeak = 0;
    for (int hits = 0; hits <= maxHits; hits += step) {
        int feasible = 0, capped = 0;
        double totalUs = 0, totalNodes = 0;
        std::uint64_t peakStates = 0;
        std::vector<double> omegas;
        Rng rng(0xBEEF0000u + static_cast<std::uint64_t>(hits));

        for (int t = 0; t < samples; ++t) {
            std::vector<int> pool(static_cast<std::size_t>(inst.cellCount()));
            for (int i = 0; i < inst.cellCount(); ++i) pool[static_cast<std::size_t>(i)] = i;
            for (int i = inst.cellCount() - 1; i > 0; --i)
                std::swap(pool[static_cast<std::size_t>(i)],
                          pool[static_cast<std::size_t>(rng.below(i + 1))]);

            std::vector<CellConstraint> cells(static_cast<std::size_t>(inst.cellCount()),
                                              CellConstraint::Free);
            for (int i = 0; i < shots; ++i)
                cells[static_cast<std::size_t>(pool[static_cast<std::size_t>(i)])] =
                    i < hits ? CellConstraint::MustBeOccupied : CellConstraint::MustBeEmpty;

            const auto t0 = std::chrono::steady_clock::now();
            const auto r = countConfigurations(inst, cells);
            totalUs +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1e6;
            peakStates = std::max(peakStates, static_cast<std::uint64_t>(r.peakStates));
            if (r.count > 0) { ++feasible; omegas.push_back(static_cast<double>(r.count)); }

            Backtracker bt(inst, cells, kNodeCap);
            const bool found = bt.solve();
            const bool ranOut = bt.nodes >= kNodeCap;
            if (ranOut) ++capped;
            totalNodes += static_cast<double>(bt.nodes);
            // The two engines must agree on the yes/no answer. A search that hit
            // the cap returned no answer at all, so it is exempt.
            if (!ranOut && found != (r.count > 0)) {
                std::printf("  *** search and DP disagree at hits=%d, sample %d ***\n", hits, t);
                std::exit(1);
            }
        }
        std::sort(omegas.begin(), omegas.end());
        const double med = omegas.empty() ? 0 : omegas[omegas.size() / 2];
        const double meanUs = totalUs / samples;
        const double meanNodes = totalNodes / samples;
        if (meanUs > dpPeak)    { dpPeak = meanUs;    dpPeakAt = hits; }
        if (meanNodes > btPeak) { btPeak = meanNodes; btPeakAt = hits; }
        std::printf("  %6d %9.1f%% %14.4g %11.0f %12llu %12.0f %10d\n", hits,
                    100.0 * feasible / samples, med, meanUs,
                    static_cast<unsigned long long>(peakStates), meanNodes, capped);
        std::fflush(stdout);
    }
    std::printf("  DP cost peaks at %d hits, search cost peaks at %d hits\n\n",
                dpPeakAt, btPeakAt);
}

void phaseTransition() {
    std::printf("2. Constraint density\n");
    std::printf("---------------------\n\n");
    std::printf("Records here are synthetic: cells are chosen at random and a fraction of\n");
    std::printf("them declared hits, with no board behind the choice. Sweeping that fraction\n");
    std::printf("carries the record from easily satisfiable to plainly impossible, and the\n");
    std::printf("cost of deciding which peaks in between.\n\n");

    sweepDensity(Instance(6, 6, {4, 3, 2}), 18, 12, 1, 400);
    sweepDensity(Instance(7, 7, {5, 4, 3, 2}), 24, 16, 2, 120);
    sweepDensity(Instance(8, 8, {5, 4, 3, 3, 2}), 34, 20, 2, 40);

    std::printf("  The two engines answer the same records and disagree about which are\n");
    std::printf("  expensive. Search cost peaks in the middle, where a record is neither\n");
    std::printf("  clearly satisfiable nor clearly not, which is the easy-hard-easy shape\n");
    std::printf("  random satisfiability shows. Sweep cost is highest where the record is\n");
    std::printf("  loosest and broadly falls as it tightens: a counting sweep never\n");
    std::printf("  backtracks, so a constraint removes work rather than adding it, and the\n");
    std::printf("  hardest record decides faster than the empty one.\n");
}

}  // namespace mayflower::m9
