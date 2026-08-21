// Exact optimal play on small instances.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "mayflower/exact_solver.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/policy.hpp"

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

using namespace mayflower;

// The optimum can never exceed what any concrete policy achieves. This is the
// solver's strongest self-check: a bug that under-counts shows up immediately as
// a heuristic beating the supposed optimum.
void testOptimumDominatesEveryPolicy() {
    std::printf("[optimum dominates every policy]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {
        {3, 3, {2}}, {4, 3, {2}}, {4, 4, {3}}, {3, 4, {2}}, {5, 3, {3}},
    };
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        const auto opt = solveOptimal(inst);

        DensityPolicy density;
        ParityHuntTarget parity;
        RandomPolicy random;
        const auto d = exactPolicyExpectation(inst, density);
        const auto p = exactPolicyExpectation(inst, parity);
        const auto r = exactPolicyExpectation(inst, random);

        check(opt.expectedShots >= inst.shipCells() - 1e-9,
              inst.describe() + " optimum respects the coverage bound");
        check(opt.expectedShots <= d.expectedShots + 1e-9,
              inst.describe() + " optimum is at most the density policy");
        check(opt.expectedShots <= p.expectedShots + 1e-9,
              inst.describe() + " optimum is at most parity hunt/target");
        check(opt.expectedShots <= r.expectedShots + 1e-9,
              inst.describe() + " optimum is at most the random shooter");
        check(opt.optimalFirstShot >= 0 && opt.optimalFirstShot < inst.cellCount(),
              inst.describe() + " reports a legal first shot");

        std::printf("  %-12s optimal %7.4f  density %7.4f  parity %7.4f  random %7.4f\n",
                    inst.describe().c_str(), opt.expectedShots, d.expectedShots, p.expectedShots,
                    r.expectedShots);
    }
}

// The expectation is a mean over an integer number of configurations, so the
// total shot count is an integer. Pinning it catches drift that a tolerance on
// the mean would hide.
void testPinnedValues() {
    std::printf("[pinned optima]\n");
    struct Pin { int w, h; std::vector<int> fleet; std::uint64_t totalShots; };
    const std::vector<Pin> pins = {
        {3, 3, {2}, 54},     // 54/12  = 4.5
        {4, 3, {2}, 87},     // 87/17  = 5.117647...
        {4, 4, {3}, 90},     // 90/16  = 5.625
    };
    for (const Pin& pin : pins) {
        const Instance inst(pin.w, pin.h, pin.fleet);
        const auto opt = solveOptimal(inst);
        const double totalShots = opt.expectedShots * static_cast<double>(opt.configurations);
        check(std::abs(totalShots - static_cast<double>(pin.totalShots)) < 1e-6,
              inst.describe() + " optimum totals " + std::to_string(pin.totalShots) + " shots");
        check(std::abs(totalShots - std::round(totalShots)) < 1e-6,
              inst.describe() + " total shot count is an integer");
        std::printf("  %-12s %llu shots over %llu configurations = %.6f\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(pin.totalShots),
                    static_cast<unsigned long long>(opt.configurations), opt.expectedShots);
    }
}

void testDeterminism() {
    std::printf("[determinism]\n");
    const Instance inst(4, 3, {2});
    const auto a = solveOptimal(inst);
    const auto b = solveOptimal(inst);
    check(a.expectedShots == b.expectedShots, "repeated solves agree bit for bit");
    check(a.optimalFirstShot == b.optimalFirstShot, "the optimal first shot is stable");
    std::printf("  two solves agree exactly\n");
}

void testRefusesOversizedInstances() {
    std::printf("[limits]\n");
    bool threw = false;
    try {
        const Instance inst = standardInstance();
        (void)solveOptimal(inst);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "the exact solver refuses the full 10x10 instance instead of hanging");
    std::printf("  10x10 is refused up front\n");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testOptimumDominatesEveryPolicy();
    testPinnedValues();
    testDeterminism();
    testRefusesOversizedInstances();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
