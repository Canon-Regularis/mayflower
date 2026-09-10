// What feedback is worth.
//
// A non-adaptive player fixes the order of the cells in advance, so the best it
// can do is the best chain through the subset lattice. The gap to the adaptive
// optimum is what feedback buys.

#include "core.hpp"

namespace mayflower::m9 {

NonAdaptive nonAdaptiveOptimum(const Instance& inst) {
    const int n = inst.cellCount();
    if (n > 22) throw std::runtime_error("subset lattice too large");
    const std::size_t size = std::size_t{1} << n;

    const std::vector<CellConstraint> free(static_cast<std::size_t>(n), CellConstraint::Free);
    const Backtracker bt(inst, free, ~0ull);
    std::vector<std::uint64_t> configs;
    enumerateMasks(bt, 0, 0, 0, configs);

    // c[S] counts configurations contained in S. Seed with the masks themselves,
    // then run the transform one bit at a time.
    std::vector<std::uint32_t> c(size, 0);
    for (std::uint64_t m : configs) ++c[static_cast<std::size_t>(m)];
    for (int b = 0; b < n; ++b)
        for (std::size_t S = 0; S < size; ++S)
            if (S & (std::size_t{1} << b)) c[S] += c[S ^ (std::size_t{1} << b)];

    const std::uint64_t N = configs.size();
    if (c[size - 1] != N) throw std::runtime_error("subset transform disagrees with the count");

    // best[S] is the largest achievable sum of c over the prefixes strictly
    // inside S, so the answer reads off the full set.
    std::vector<std::uint64_t> best(size, 0);
    for (std::size_t S = 1; S < size; ++S) {
        std::uint64_t b = 0;
        std::size_t bits = S;
        while (bits) {
            const std::size_t low = bits & (~bits + 1);
            const std::size_t prev = S ^ low;
            b = std::max(b, best[prev] + c[prev]);
            bits ^= low;
        }
        best[S] = b;
    }

    // The greedy order takes whichever cell raises the covered count most.
    std::uint64_t greedySum = 0;
    std::size_t cur = 0;
    for (int t = 0; t < n; ++t) {
        greedySum += c[cur];
        std::size_t bestCell = 0;
        std::uint32_t bestGain = 0;
        bool first = true;
        for (int b = 0; b < n; ++b) {
            const std::size_t bit = std::size_t{1} << b;
            if (cur & bit) continue;
            if (first || c[cur | bit] > bestGain) {
                bestGain = c[cur | bit];
                bestCell = bit;
                first = false;
            }
        }
        cur |= bestCell;
    }

    NonAdaptive out;
    out.configurations = N;
    out.optimal = n - static_cast<double>(best[size - 1]) / static_cast<double>(N);
    out.greedy  = n - static_cast<double>(greedySum)     / static_cast<double>(N);
    return out;
}

constexpr std::uint64_t kAdaptiveLimit = 300;

void adaptivityGap() {
    std::printf("3. The adaptivity gap\n");
    std::printf("---------------------\n\n");
    std::printf("What does feedback buy? A non-adaptive player fixes the order of the cells\n");
    std::printf("before play and never looks at an answer. An adaptive player sees each\n");
    std::printf("outcome before choosing again. Both are solved exactly here, so the ratio\n");
    std::printf("is a measurement rather than a bound.\n\n");

    std::printf("%-11s %8s %11s %11s %11s %8s %7s\n", "instance", "boards", "adaptive",
                "fixed order", "greedy order", "gap", "ratio");
    struct C { int w, h; std::vector<int> f; };
    for (const C& k : std::vector<C>{{3,3,{2}},{4,3,{2}},{4,4,{2}},{4,4,{3}},{4,4,{4}},
                                     {5,4,{3}},{4,4,{2,2}},{4,4,{3,2}},{5,4,{3,2}},
                                     {5,4,{4,3,2}}}) {
        const Instance inst(k.w, k.h, k.f);
        NonAdaptive na;
        ExactSolution ad;
        try {
            na = nonAdaptiveOptimum(inst);
        } catch (const std::exception&) { continue; }

        // The belief MDP is the binding cost here, so instances past its reach
        // report the fixed order alone.
        bool solved = true;
        try {
            ad = solveOptimal(inst, kAdaptiveLimit);
        } catch (const std::exception&) { solved = false; }

        char adaptive[16] = "-", gap[16] = "-", ratio[16] = "-";
        if (solved) {
            std::snprintf(adaptive, sizeof adaptive, "%.4f", ad.expectedShots);
            std::snprintf(gap, sizeof gap, "%.4f", na.optimal - ad.expectedShots);
            std::snprintf(ratio, sizeof ratio, "%.4f", na.optimal / ad.expectedShots);
        }
        std::printf("%-11s %8llu %11s %11.4f %11.4f %8s %7s\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(na.configurations), adaptive,
                    na.optimal, na.greedy, gap, ratio);
        std::fflush(stdout);
    }
    std::printf("\nThe fixed order column is the exact optimum over all n! orders, reached\n");
    std::printf("through the subset lattice: an order's clearing time depends on its prefixes\n");
    std::printf("as sets, so the best order is the best chain, and 2^n beats n!.\n\n");
    std::printf("Feedback is worth most against a lone ship. One 3-ship on 5x4 costs 6.23\n");
    std::printf("shots with feedback and 13.05 without, a ratio of 2.09, the largest here.\n");
    std::printf("Fleets score lower: 4x4 {2,2} gives 1.44 and 4x4 {3,2} gives 1.50. What\n");
    std::printf("feedback buys is the right to skip cells, and a fleet covering more of the\n");
    std::printf("board leaves fewer worth skipping.\n\n");
    std::printf("The fixed-order column shows the same thing from the other side. On 5x4 it\n");
    std::printf("runs 13.05, 15.89 and 18.14 of 20 cells as the fleet grows, so a player\n");
    std::printf("with no feedback ends up shooting nearly the whole board. That column stops\n");
    std::printf("at 20 cells because the lattice is 2^n; the adaptive column stops at 264\n");
    std::printf("boards, which is the belief MDP and a much harder wall.\n\n");
    std::printf("The greedy order, taking the cell that covers the most configurations still\n");
    std::printf("uncovered, lands within 3.2%% of the optimum on every instance here. The\n");
    std::printf("familiar 4-approximation covers min-sum set cover, where a set is paid for\n");
    std::printf("at its first covered element. This objective waits for the last one, which\n");
    std::printf("is the K(S)=|S| case of generalized min-sum set cover and carries no such\n");
    std::printf("guarantee, so 3.2%% is an observation about these instances. See\n");
    std::printf("docs/COMPLEXITY.md.\n\n");
}

}  // namespace mayflower::m9
