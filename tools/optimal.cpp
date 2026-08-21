// optimal: exact optimal play on small instances, and the measured optimality
// gap of each heuristic against it.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <cstdio>
#include <memory>
#include <vector>

#include "mayflower/exact_solver.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/policy.hpp"

int main() {
    using namespace mayflower;

    std::printf("Exact optimal play on small instances\n");
    std::printf("=====================================\n\n");
    std::printf("Every configuration is enumerated, so both the optimum and each policy's\n");
    std::printf("expectation are exact. No sampling, no intervals.\n\n");

    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {
        {3, 3, {2}},
        {4, 3, {2}},
        {4, 4, {2}},
        {4, 4, {3}},
        {5, 4, {3}},
        {4, 4, {2, 2}},
        {4, 4, {3, 2}},
    };

    std::printf("%-14s %6s %10s %10s %8s %11s %8s %11s %8s\n", "instance", "cfgs", "optimal",
                "density", "gap", "max-P(hit)", "gap", "max-info", "gap");
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        ExactSolution opt;
        try {
            opt = solveOptimal(inst);
        } catch (const std::exception& e) {
            std::printf("%-16s skipped: %s\n", inst.describe().c_str(), e.what());
            continue;
        }

        DensityPolicy density;
        ExactPolicy maxProb(Objective::MaxHitProbability);
        ExactPolicy maxInfo(Objective::MaxInformationGain);
        const auto d = exactPolicyExpectation(inst, density);
        const auto a = exactPolicyExpectation(inst, maxProb);
        const auto b = exactPolicyExpectation(inst, maxInfo);

        const auto gap = [&](double v) { const double g = v - opt.expectedShots;
                                        return std::abs(g) < 1e-9 ? 0.0 : g; };
        std::printf("%-14s %6llu %10.6f %10.6f %8.4f %11.6f %8.4f %11.6f %8.4f\n",
                    inst.describe().c_str(),
                    static_cast<unsigned long long>(opt.configurations), opt.expectedShots,
                    d.expectedShots, gap(d.expectedShots), a.expectedShots, gap(a.expectedShots),
                    b.expectedShots, gap(b.expectedShots));
        // Totals are integers, so the gap is exact and not a rounding artefact.
        std::printf("%-14s   total shots over the space: optimal %.0f, max-P(hit) %.0f, "
                    "max-info %.0f\n", "",
                    opt.expectedShots * static_cast<double>(opt.configurations),
                    a.expectedShots * static_cast<double>(a.configurations),
                    b.expectedShots * static_cast<double>(b.configurations));
        std::printf("%-16s   optimal first shot: cell %d (row %d, col %d); %llu memo states, %.2f s\n",
                    "", opt.optimalFirstShot, opt.optimalFirstShot / c.w, opt.optimalFirstShot % c.w,
                    static_cast<unsigned long long>(opt.memoStates), opt.seconds);
    }

    std::printf("\nEach gap is the exact price of that objective on that instance, from integer\n");
    std::printf("totals over the enumerated space, so none of it is sampling error.\n\n");
    std::printf("Maximising one-step information gain is far worse than maximising hit\n");
    std::printf("probability, by shots and not by fractions of one. That is the coverage-limited\n");
    std::printf("thesis showing up directly: identifying the board is cheap, and spending shots\n");
    std::printf("to identify it instead of to cover it wastes them. Maximising hit probability is\n");
    std::printf("close to optimal, and exactly optimal on several instances, but it is not\n");
    std::printf("optimal in general.\n");
    return 0;
}
