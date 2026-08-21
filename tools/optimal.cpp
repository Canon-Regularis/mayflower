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

    std::printf("%-16s %6s %10s %9s %9s %9s %9s\n", "instance", "cfgs", "optimal", "density",
                "gap", "parity", "gap");
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
        ParityHuntTarget parity;
        const auto d = exactPolicyExpectation(inst, density);
        const auto p = exactPolicyExpectation(inst, parity);

        const auto gap = [&](double v) { const double g = v - opt.expectedShots;
                                        return std::abs(g) < 1e-9 ? 0.0 : g; };
        std::printf("%-16s %6llu %10.4f %9.4f %9.4f %9.4f %9.4f\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(opt.configurations), opt.expectedShots,
                    d.expectedShots, gap(d.expectedShots), p.expectedShots,
                    gap(p.expectedShots));
        std::printf("%-16s   optimal first shot: cell %d (row %d, col %d); %llu memo states, %.2f s\n",
                    "", opt.optimalFirstShot, opt.optimalFirstShot / c.w, opt.optimalFirstShot % c.w,
                    static_cast<unsigned long long>(opt.memoStates), opt.seconds);
    }

    std::printf("\nThe gap column is the exact price of the heuristic on that instance. It is the\n");
    std::printf("same quantity the 10x10 report estimates, measured here where the optimum is\n");
    std::printf("computable.\n");
    return 0;
}
