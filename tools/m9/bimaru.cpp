// What row and column sums cost.
//
// Bimaru is this board with the occupied count of every row and column given.
// The input splits by sweep direction: a column sum costs a factor, a row sum
// needs every row counter carried at once.

#include "core.hpp"

namespace mayflower::m9 {

void bimaruCost() {
    std::printf("4. What row and column sums cost\n");
    std::printf("--------------------------------\n\n");
    std::printf("Bimaru is this board with the occupied count of every row and column given.\n");
    std::printf("The sweep runs column-major, so a column sum is settled inside its column:\n");
    std::printf("one counter of 0..H, checked and cleared at the boundary, multiplying the\n");
    std::printf("state by H+1. A row sum accumulates the whole way across, so every row\n");
    std::printf("counter has to be carried at once, and the multiplier is the number of\n");
    std::printf("distinct partial row-count vectors a cut can see.\n\n");

    std::printf("%-13s %8s %6s %14s %14s\n", "instance", "boards", "cut",
                "row vectors", "column sums");
    struct C { int w, h; std::vector<int> f; };
    for (const C& k : std::vector<C>{{4,4,{3,2}},{5,5,{4,3,2}},{6,6,{4,3,2}},
                                     {6,6,{4,3,3,2}}}) {
        const Instance inst(k.w, k.h, k.f);
        const std::vector<CellConstraint> free(static_cast<std::size_t>(inst.cellCount()),
                                               CellConstraint::Free);
        const Backtracker bt(inst, free, ~0ull);
        std::vector<std::uint64_t> configs;
        enumerateMasks(bt, 0, 0, 0, configs);

        std::size_t worst = 0;
        int worstCut = 0;
        for (int cut = 1; cut < inst.width; ++cut) {
            std::set<std::vector<std::uint8_t>> seen;
            for (std::uint64_t m : configs) {
                std::vector<std::uint8_t> rows(static_cast<std::size_t>(inst.height), 0);
                for (int r = 0; r < inst.height; ++r)
                    for (int c = 0; c < cut; ++c)
                        if (m & (1ull << inst.cellIndex(r, c)))
                            ++rows[static_cast<std::size_t>(r)];
                seen.insert(rows);
            }
            if (seen.size() > worst) { worst = seen.size(); worstCut = cut; }
        }
        std::printf("%-13s %8llu %6d %14llu %14d\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(configs.size()), worstCut,
                    static_cast<unsigned long long>(worst), inst.height + 1);
        std::fflush(stdout);
    }

    // The standard instance is past enumeration, so bound the vectors instead:
    // each row holds 0..5 occupied cells in half a board and the fleet supplies
    // 17 in total.
    std::vector<std::uint64_t> ways(18, 0);
    ways[0] = 1;
    for (int r = 0; r < 10; ++r) {
        std::vector<std::uint64_t> next(18, 0);
        for (int s2 = 0; s2 <= 17; ++s2)
            if (ways[static_cast<std::size_t>(s2)])
                for (int add = 0; add <= 5 && s2 + add <= 17; ++add)
                    next[static_cast<std::size_t>(s2 + add)] += ways[static_cast<std::size_t>(s2)];
        ways = next;
    }
    std::uint64_t bound = 0;
    for (std::uint64_t v : ways) bound += v;
    std::printf("\n  10x10 {5,4,3,3,2} at the half-board cut, upper bound %llu row vectors.\n",
                static_cast<unsigned long long>(bound));
    std::printf("  The peak profile state is 376,735, so the product is 1.9e12 and the sweep\n");
    std::printf("  is finished.\n\n");
    std::printf("  So the two halves of Bimaru's input split cleanly. Column sums are nearly\n");
    std::printf("  free in this sweep direction and row sums are not, and transposing the\n");
    std::printf("  sweep only swaps which half is which. Sevenster's NP-completeness proof\n");
    std::printf("  for the puzzle sits on the other side of that split, so the plan's\n");
    std::printf("  estimate of one day and a hundred lines held only for the free half.\n\n");
}

}  // namespace mayflower::m9
