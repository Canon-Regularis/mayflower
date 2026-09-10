// The invariants every extension must satisfy.
//
// Twenty seven of them, including that feedback never hurts, that a worst case
// never sits below an average, that greedy never beats the optimal order, and
// that the backtracking search and the sweep agree on feasibility. This is the
// safety net for the split: if the eight files still satisfy all of it, nothing
// moved that mattered.

#include "core.hpp"

namespace mayflower::m9 {

// Invariants the sections above rest on, cheap enough to run in the fast suite.
int selfTest() {
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::printf("  %-58s %s\n", what, ok ? "ok" : "FAILED");
        if (!ok) ++failures;
    };
    std::printf("m9 self-test\n------------\n");

    struct C { int w, h; std::vector<int> f; };
    for (const C& k : std::vector<C>{{3,3,{2}},{4,3,{2}},{4,4,{2}},{4,4,{2,2}},{4,4,{3,2}}}) {
        const Instance inst(k.w, k.h, k.f);
        const NonAdaptive na = nonAdaptiveOptimum(inst);

        // The enumerator and the sweep are independent implementations.
        const std::uint64_t swept = countConfigurations(inst).count;
        check(na.configurations == swept,
              (inst.describe() + ": enumeration matches the sweep").c_str());

        // Feedback cannot hurt, and greedy cannot beat the optimum.
        const ExactSolution ad = solveOptimal(inst, 300);
        check(ad.expectedShots <= na.optimal + 1e-9,
              (inst.describe() + ": adaptive <= fixed order").c_str());
        check(na.greedy >= na.optimal - 1e-9,
              (inst.describe() + ": greedy >= optimal fixed order").c_str());

        // A worst case is never below an average. The minimax solve prunes far
        // worse than the expectation one, so this rung stops early enough to
        // keep the whole self-test inside the fast suite.
        if (na.configurations <= 30) {
            const ExactSolution adv = solveOptimal(inst, 300, Adversary::Adaptive);
            check(adv.expectedShots >= ad.expectedShots - 1e-9,
                  (inst.describe() + ": adversarial >= committed").c_str());
            check(std::abs(adv.expectedShots - std::round(adv.expectedShots)) < 1e-9,
                  (inst.describe() + ": adversarial worst case is an integer").c_str());
        }

        // Every board is cleared, so no optimum beats the ship-cell count.
        check(ad.expectedShots >= inst.shipCells() - 1e-9,
              (inst.describe() + ": adaptive >= coverage bound").c_str());
    }

    // The backtracking search and the sweep must agree on feasibility.
    {
        const Instance inst(4, 4, {3, 2});
        Rng rng(7);
        int agreed = 0;
        for (int t = 0; t < 300; ++t) {
            std::vector<CellConstraint> cells(16, CellConstraint::Free);
            for (int i = 0; i < 16; ++i) {
                const int r = rng.below(4);
                if (r == 0) cells[static_cast<std::size_t>(i)] = CellConstraint::MustBeOccupied;
                else if (r == 1) cells[static_cast<std::size_t>(i)] = CellConstraint::MustBeEmpty;
            }
            Backtracker bt(inst, cells, kNodeCap);
            if (bt.solve() == (countConfigurations(inst, cells).count > 0)) ++agreed;
        }
        check(agreed == 300, "search and sweep agree on 300 random records");
    }

    // The noisy posterior is exactly Boltzmann in the mismatch count.
    {
        const Instance inst(4, 4, {3, 2});
        const std::vector<CellConstraint> blank(16, CellConstraint::Free);
        const Backtracker bt(inst, blank, ~0ull);
        std::vector<std::uint64_t> configs;
        enumerateMasks(bt, 0, 0, 0, configs);
        const double eps = 0.13, beta = std::log((1 - eps) / eps);
        Rng rng(99);
        const std::size_t truth =
            static_cast<std::size_t>(rng.below(static_cast<int>(configs.size())));
        std::vector<double> w(configs.size(), 1.0);
        std::vector<int> mismatch(configs.size(), 0);
        for (int t = 0; t < 25; ++t) {
            const int cell = rng.below(16);
            const bool answer = ((configs[truth] >> cell) & 1ull) != 0;
            for (std::size_t i = 0; i < configs.size(); ++i) {
                if ((((configs[i] >> cell) & 1ull) != 0) == answer) w[i] *= (1 - eps);
                else { w[i] *= eps; ++mismatch[i]; }
            }
        }
        double za = 0, zb = 0, worst = 0;
        for (std::size_t i = 0; i < configs.size(); ++i) {
            za += w[i];
            zb += std::exp(-beta * mismatch[i]);
        }
        for (std::size_t i = 0; i < configs.size(); ++i)
            worst = std::max(worst, std::abs(w[i] / za - std::exp(-beta * mismatch[i]) / zb));
        check(worst < 1e-12, "noisy posterior equals exp(-beta m)/Z");
    }

    std::printf("\n%s\n", failures ? "SELF-TEST FAILED" : "all invariants hold");
    return failures ? 1 : 0;
}

}  // namespace mayflower::m9
