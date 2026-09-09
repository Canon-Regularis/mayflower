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

#include "harness.hpp"

namespace {

using mf::test::expect;
using mf::test::gChecks;
using mf::test::gFailures;

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

        expect(opt.expectedShots >= inst.shipCells() - 1e-9,
              inst.describe() + " optimum respects the coverage bound");
        expect(opt.expectedShots <= d.expectedShots + 1e-9,
              inst.describe() + " optimum is at most the density policy");
        expect(opt.expectedShots <= p.expectedShots + 1e-9,
              inst.describe() + " optimum is at most parity hunt/target");
        expect(opt.expectedShots <= r.expectedShots + 1e-9,
              inst.describe() + " optimum is at most the random shooter");
        expect(opt.optimalFirstShot >= 0 && opt.optimalFirstShot < inst.cellCount(),
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
        expect(std::abs(totalShots - static_cast<double>(pin.totalShots)) < 1e-6,
              inst.describe() + " optimum totals " + std::to_string(pin.totalShots) + " shots");
        expect(std::abs(totalShots - std::round(totalShots)) < 1e-6,
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
    expect(a.expectedShots == b.expectedShots, "repeated solves agree bit for bit");
    expect(a.optimalFirstShot == b.optimalFirstShot, "the optimal first shot is stable");
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
    expect(threw, "the exact solver refuses the full 10x10 instance instead of hanging");
    std::printf("  10x10 is refused up front\n");
}

// Pruning may make the search cheaper and may not make it wrong. Every level
// charges unevaluated branches at an admissible floor, so raising that floor
// above the truth can prune the optimum and return a larger answer while every
// other test still passes: the pinned optima are computed at the default level,
// so they move together with the bug.
//
// The instances are the cheap end of the ladder on purpose. Unpruned search is
// exponential, and 4x4 {2} already costs seconds at level None.
// Instances at the edge of what the solver can be asked about. An instance
// with one configuration is settled at the root, so value() returned before
// it ever chose a cell and left the caller holding the -1 sentinel as an
// "optimal opening". An instance with none reached front() on an empty
// support and aborted inside the solver, and the policy evaluator averaged
// over nothing and returned NaN, which compares false against every bound a
// caller might test it against.
// Every prune in the search rests on floorOf being a lower bound on the value
// it stands in for, and nothing checked that. Comparing pruning levels does not
// reach it: a floor inflated by 0.5 changed no answer on any instance small
// enough to solve, because away from the endgame the value exceeds the floor by
// the expected misses and absorbs the error. The states with no slack at all are
// the settled ones, where the ship cells are known and the value IS the unshot
// count, and there an error of 0.001 is visible. That is where this looks.
void testFloorIsAdmissible() {
    std::printf("[the pruning floor is a lower bound]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const Case cases[] = {
        {3, 3, {2}}, {4, 3, {2}}, {4, 4, {3}}, {3, 4, {2}}, {4, 4, {2}},
    };
    for (const Case& c : cases) {
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        for (const auto level : {mayflower::Pruning::Bounds, mayflower::Pruning::Star1}) {
            const auto sol = mayflower::solveOptimal(
                inst, 60000, mayflower::Adversary::Committed, level, /*auditFloor=*/true);
            expect(sol.admissibilityViolations == 0,
                  inst.describe() + ": the floor never exceeds the value it bounds");
            if (sol.admissibilityViolations != 0)
                std::printf("      %llu nodes where floorOf came out above the exact value\n",
                            static_cast<unsigned long long>(sol.admissibilityViolations));
        }
    }
    std::printf("  %d instances audited at two pruning levels, no violation\n",
                static_cast<int>(sizeof(cases) / sizeof(cases[0])));
}

void testDegenerateInstances() {
    std::printf("[instances at the edge]\n");

    // Settled at the root: the ship cells are known, so the opening is any of
    // them and must be a real cell.
    struct Settled { int w, h; std::vector<int> fleet; double shots; };
    const Settled settled[] = {
        {1, 1, {1}, 1.0},
        {1, 4, {4}, 4.0},
        {3, 3, {3, 3, 3}, 9.0},
    };
    for (const Settled& c : settled) {
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        const auto sol = mayflower::solveOptimal(inst);
        expect(std::abs(sol.expectedShots - c.shots) < 1e-9,
              inst.describe() + ": every cell must be shot");
        expect(sol.optimalFirstShot >= 0 && sol.optimalFirstShot < inst.cellCount(),
              inst.describe() + ": names a real opening rather than -1");
    }

    // A fleet that cannot be placed validates, so only the solver can refuse.
    const mayflower::Instance empty(2, 2, {2, 2, 2});
    ++gChecks;
    try {
        const auto sol = mayflower::solveOptimal(empty);
        ++gFailures;
        std::printf("  FAIL  solved an empty space, E[T] %.4f\n", sol.expectedShots);
    } catch (const std::invalid_argument&) {
        std::printf("  an empty configuration space is refused\n");
    }

    ++gChecks;
    try {
        mayflower::DensityPolicy p;
        const auto r = mayflower::exactPolicyExpectation(empty, p);
        ++gFailures;
        std::printf("  FAIL  averaged a policy over nothing, E[T] %.4f\n",
                    r.expectedShots);
    } catch (const std::invalid_argument&) {
        std::printf("  and a policy cannot be averaged over it either\n");
    }
}

void testPruningLevelsAgree() {
    std::printf("[pruning does not change the answer]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const Case cases[] = {
        {3, 3, {2}}, {4, 3, {2}}, {4, 4, {3}}, {3, 4, {2}}, {4, 4, {2}},
    };
    const mayflower::Pruning levels[] = {
        mayflower::Pruning::None, mayflower::Pruning::Bounds, mayflower::Pruning::Star1,
    };
    const char* names[] = {"None", "Bounds", "Star1"};

    for (const Case& c : cases) {
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        double shots[3] = {0, 0, 0};
        int first[3] = {-1, -1, -1};   // recorded, deliberately not compared
        std::uint64_t nodes[3] = {0, 0, 0};
        std::uint64_t cut[3] = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            const auto sol = mayflower::solveOptimal(inst, 60000,
                                                    mayflower::Adversary::Committed, levels[i]);
            shots[i] = sol.expectedShots;
            first[i] = sol.optimalFirstShot;
            nodes[i] = sol.nodesExpanded;
            cut[i] = sol.branchesCut;
        }
        const bool sameValue = std::abs(shots[0] - shots[1]) < 1e-12
                            && std::abs(shots[0] - shots[2]) < 1e-12;
        expect(sameValue, inst.describe() + ": every pruning level returns one optimum");
        if (!sameValue)
            std::printf("      %s %.10f, %s %.10f, %s %.10f\n",
                        names[0], shots[0], names[1], shots[1], names[2], shots[2]);
        // Only the value is compared. The optimal opening is not unique, so the
        // levels may legitimately name different cells: on 3x3 {2} five of the
        // nine cells open at 4.5, and None and Star1 pick different ones.
        (void)first;

        // Agreeing on the answer says nothing about whether the bounds removed
        // any work, and removing work is the only reason the levels exist. A
        // change that stops the pruning leaves every answer correct and costs
        // only time, which the comparison above cannot see. Wall-clock is the
        // wrong instrument here, having already picked the wrong default twice
        // from single runs taken on a busy machine. These counts are exact and
        // do not vary with the machine.
        expect(nodes[0] > nodes[1],
               inst.describe() + ": Bounds expands fewer nodes than None");
        expect(nodes[1] > nodes[2],
               inst.describe() + ": Star1 expands fewer nodes than Bounds");
        // A stronger bound reaches the incumbent sooner, so it abandons more
        // chance branches part-way rather than fewer. This also pins what
        // counts as abandoned, since a branch set that ran to completion was
        // not abandoned at all.
        expect(cut[0] < cut[1],
               inst.describe() + ": Bounds abandons more branches than None");
    }
}

// Every case above runs the committed adversary, so none of them reach the
// adaptive chance-node bound, which is a separate piece of code with its own
// cell test and its own branch cut. Both can be switched off without changing
// one answer: the optima here are integers, so the running bound meets the
// incumbent exactly, and a comparison that stops being inclusive prunes
// nothing at all.
void testAdaptivePruningStaysOn() {
    std::printf("[the adaptive bound still prunes]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const Case cases[] = {{3, 3, {2}}, {4, 3, {2}}, {4, 4, {3}}};
    const mayflower::Pruning levels[] = {
        mayflower::Pruning::None, mayflower::Pruning::Bounds, mayflower::Pruning::Star1,
    };
    const char* names[] = {"None", "Bounds", "Star1"};

    for (const Case& c : cases) {
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        for (int i = 0; i < 3; ++i) {
            const auto sol = mayflower::solveOptimal(inst, 60000,
                                                    mayflower::Adversary::Adaptive, levels[i]);
            const std::string what = inst.describe() + " adaptive " + names[i];
            expect(sol.cellsPruned > 0, what + ": the cell bound rejects candidates");
            expect(sol.branchesCut > 0, what + ": the branch bound cuts branches");
        }
    }
    std::printf("  both adaptive mechanisms fire at every level\n");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testOptimumDominatesEveryPolicy();
    testPinnedValues();
    testDeterminism();
    testRefusesOversizedInstances();
    testFloorIsAdmissible();
    testDegenerateInstances();
    testPruningLevelsAgree();
    testAdaptivePruningStaysOn();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
