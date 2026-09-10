// The adaptive adversary.
//
// A committed hider fixes the board first, so the searcher faces an average. An
// adaptive hider answers each shot to hurt most while staying consistent, which
// turns the chance node into a maximum and the result into a worst case.

#include "core.hpp"

namespace mayflower::m9 {

void adversary() {
    std::printf("1. The adaptive adversary\n");
    std::printf("-------------------------\n\n");
    std::printf("A committed hider fixes the board first, so the searcher faces an average.\n");
    std::printf("An adaptive hider answers each shot to hurt most while staying consistent\n");
    std::printf("with everything already said, so the searcher faces a worst case that no\n");
    std::printf("distributional assumption can soften.\n\n");

    std::printf("%-13s %7s %14s %11s %8s %8s\n", "instance", "boards", "E[T] committed",
                "W* adaptive", "gap", "beta(L)");
    struct C { int w, h; std::vector<int> f; };
    for (const C& c : std::vector<C>{{3,3,{2}},{4,3,{2}},{4,4,{2}},{4,4,{3}},
                                     {5,4,{3}},{4,4,{2,2}},{4,4,{3,2}}}) {
        const Instance inst(c.w, c.h, c.f);
        ExactSolution e, a;
        try {
            e = solveOptimal(inst, 60000, Adversary::Committed);
            a = solveOptimal(inst, 60000, Adversary::Adaptive);
        } catch (const std::exception&) { continue; }

        char beta[16] = "-";
        if (c.f.size() == 1)
            std::snprintf(beta, sizeof beta, "%d",
                          blockingNumber(c.w, c.h, c.f[0]).blocking);
        std::printf("%-13s %7llu %14.4f %11.0f %8.2f %8s\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(e.configurations), e.expectedShots,
                    a.expectedShots, a.expectedShots - e.expectedShots, beta);
        std::fflush(stdout);
    }
    std::printf("\nFor a lone ship the worst case sits above beta(L), the shots that guarantee\n");
    std::printf("first contact, by the cost of finishing the ship once found. The margin is\n");
    std::printf("not a fixed offset: the adversary also chooses which way the ship runs.\n\n");
}

}  // namespace mayflower::m9
