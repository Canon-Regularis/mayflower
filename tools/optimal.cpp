// optimal: exact optimal play on small instances, and the measured optimality
// gap of each heuristic against it.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <string>
#include <memory>
#include <vector>

#include "mayflower/exact_solver.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/policy.hpp"


// What the pruning buys, and how much further it carries the solver.
//
// Star1 keeps a running lower bound on a chance node by charging branches that
// have not been evaluated at their admissible floor rather than at zero, and
// tries cells in descending hit probability so a tight incumbent arrives first.
// Neither changes the answer, which is the first column below.
void pruningLadder() {
    std::printf("\nWhat the pruning buys\n---------------------\n\n");
    std::printf("Three levels, each adding one mechanism, so the change is attributable.\n");
    std::printf("None is the original search. Bounds charges branches that have not been\n");
    std::printf("evaluated at their admissible floor instead of at zero, so the running value\n");
    std::printf("stays a valid lower bound and cuts earlier. Star1 adds move ordering.\n\n");
    std::printf("None of them may change the answer, which the last column checks.\n\n");

    std::printf("%-12s %7s %10s %9s %9s %9s %11s %6s\n", "instance", "boards", "E[T]",
                "none s", "bounds s", "star1 s", "star1 nodes", "agree");

    struct C { int w, h; std::vector<int> f; bool slow; };
    for (const C& c : std::vector<C>{{3, 3, {2}, false}, {4, 3, {2}, false},
                                     {4, 4, {2}, false}, {4, 4, {3}, false},
                                     {5, 4, {3}, true}, {4, 4, {2, 2}, true},
                                     {4, 4, {3, 2}, true}}) {
        const mayflower::Instance inst(c.w, c.h, c.f);
        mayflower::ExactSolution none, bounds, star1;
        try {
            star1 = mayflower::solveOptimal(inst, 60000, mayflower::Adversary::Committed,
                                            mayflower::Pruning::Star1);
            // On the fleet instances the weaker levels are not slow, they are
            // hours: 4x4 {3,2} takes about three under Bounds against a minute
            // under Star1. Running them here would make the tool unusable, and
            // the gap is the finding rather than a cost worth paying twice.
            if (!c.slow) {
                bounds = mayflower::solveOptimal(inst, 60000, mayflower::Adversary::Committed,
                                                 mayflower::Pruning::Bounds);
                none = mayflower::solveOptimal(inst, 60000, mayflower::Adversary::Committed,
                                               mayflower::Pruning::None);
            }
        } catch (const std::exception&) { continue; }

        char noneCell[16] = "-", boundsCell[16] = "-", agreeCell[12] = "-";
        if (!c.slow) {
            std::snprintf(noneCell, sizeof noneCell, "%.3f", none.seconds);
            std::snprintf(boundsCell, sizeof boundsCell, "%.3f", bounds.seconds);
            const bool agree =
                std::abs(bounds.expectedShots - star1.expectedShots) < 1e-12 &&
                std::abs(none.expectedShots - star1.expectedShots) < 1e-12;
            std::snprintf(agreeCell, sizeof agreeCell, "%s", agree ? "yes" : "*** NO ***");
        }
        std::printf("%-12s %7llu %10.6f %9s %9s %9.3f %11llu %6s\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(star1.configurations), star1.expectedShots,
                    noneCell, boundsCell, star1.seconds,
                    static_cast<unsigned long long>(star1.nodesExpanded), agreeCell);
        std::fflush(stdout);
    }
    std::printf("\nA dash is a run that was not attempted, not a failure. The fleet instances\n");
    std::printf("are dashed because the weaker levels take hours there rather than minutes,\n");
    std::printf("which is itself the result: 4x4 {3,2} solves in about a minute under Star1\n");
    std::printf("and took just under three hours under Bounds. The ordering is not a\n");
    std::printf("tie-break on the instances anyone waits for.\n");
    std::printf("\nEquality of the answers is checked wherever more than one level ran, and\n");
    std::printf("across every level by tests/test_exact.cpp on the pinned optima.\n");
}

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const bool all = only.empty();
    if (only == "pruning") { pruningLadder(); return 0; }
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
    if (all) pruningLadder();
    return 0;
}
