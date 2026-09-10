// Salvo feedback.
//
// Classic feedback names the cell, so the record stays one constraint set.
// Salvo feedback is a count, so a turn answering h of k splits the record C(k,h)
// ways and the belief becomes a union. How fast that union grows is measured
// here rather than guessed.

#include "core.hpp"

namespace mayflower::m9 {

// 5. Salvo. Fire k cells and hear how many hit, without hearing which.
//
// Classic feedback names the cell, so the record stays one constraint set and one
// sweep prices it. Salvo feedback is a count, so a turn answering h of k splits
// the record C(k,h) ways and the belief becomes a union. Infeasible branches die,
// so how fast the union grows is a measurement rather than a guess.
struct SalvoStats {
    double turns = 0, splits = 0, sweeps = 0, shots = 0;
    std::size_t peakUnion = 0;
    int capped = 0;
};

SalvoStats salvoRun(const Instance& inst, const std::vector<std::uint64_t>& configs,
                    int k, int games, std::size_t unionCap) {
    const std::vector<CellConstraint> blank(static_cast<std::size_t>(inst.cellCount()),
                                            CellConstraint::Free);
    SalvoStats out;
    Rng rng(UINT64_C(0x5A1F0) + static_cast<std::uint64_t>(inst.cellCount() * 8 + k));

    for (int g = 0; g < games; ++g) {
        const std::uint64_t truth = configs[static_cast<std::size_t>(
            rng.below(static_cast<int>(configs.size())))];
        std::vector<std::vector<CellConstraint>> branches{blank};
        std::uint64_t shot = 0;
        bool hitCap = false;

        while ((truth & ~shot) != 0) {
            std::vector<int> open;
            for (int c = 0; c < inst.cellCount(); ++c)
                if (!(shot & (1ull << c))) open.push_back(c);
            if (static_cast<int>(open.size()) < k) break;

            // Draw k distinct cells by partial shuffle.
            for (int i = 0; i < k; ++i)
                std::swap(open[static_cast<std::size_t>(i)],
                          open[static_cast<std::size_t>(
                              i + rng.below(static_cast<int>(open.size()) - i))]);
            std::uint64_t fired = 0;
            for (int i = 0; i < k; ++i) fired |= 1ull << open[static_cast<std::size_t>(i)];
            const int hits = popcount64(truth & fired);
            out.turns += 1;
            out.shots += k;
            if (hits > 0 && hits < k) out.splits += 1;

            // Every subset of the fired cells with the announced size is a
            // candidate assignment, and each surviving branch fans out over all
            // of them.
            std::vector<std::uint32_t> assignments;
            for (std::uint32_t m = 0; m < (1u << k); ++m)
                if (popcount64(m) == hits) assignments.push_back(m);

            std::vector<std::vector<CellConstraint>> next;
            for (const auto& branch : branches) {
                for (std::uint32_t asg : assignments) {
                    std::vector<CellConstraint> child = branch;
                    for (int i = 0; i < k; ++i)
                        child[static_cast<std::size_t>(open[static_cast<std::size_t>(i)])] =
                            (asg >> i) & 1u ? CellConstraint::MustBeOccupied
                                            : CellConstraint::MustBeEmpty;
                    out.sweeps += 1;
                    if (countConfigurations(inst, child).count > 0)
                        next.push_back(std::move(child));
                }
                if (next.size() > unionCap) { hitCap = true; break; }
            }
            branches.swap(next);
            out.peakUnion = std::max(out.peakUnion, branches.size());
            shot |= fired;
            if (hitCap) break;
        }
        if (hitCap) ++out.capped;
    }
    out.turns /= games;
    out.splits /= games;
    out.sweeps /= games;
    out.shots /= games;
    return out;
}

void salvo() {
    std::printf("5. Salvo, and where the approach stops\n");
    std::printf("--------------------------------------\n\n");
    std::printf("Firing k cells and hearing only how many hit leaves the record ambiguous\n");
    std::printf("whenever the answer is neither 0 nor k. The posterior becomes a union of\n");
    std::printf("constraint sets, each needing its own sweep. Dead branches drop out, so the\n");
    std::printf("growth rate is worth measuring rather than bounding.\n\n");
    std::printf("Shots are random here, so what follows is a property of the rules and not\n");
    std::printf("of a policy. Classic play is the k=1 row, where the answer names the cell.\n\n");

    struct C { int w, h; std::vector<int> f; };
    for (const C& k : std::vector<C>{{4,4,{3,2}},{5,5,{4,3,2}}}) {
        const Instance inst(k.w, k.h, k.f);
        const std::vector<CellConstraint> blank(static_cast<std::size_t>(inst.cellCount()),
                                                CellConstraint::Free);
        const Backtracker bt(inst, blank, ~0ull);
        std::vector<std::uint64_t> configs;
        enumerateMasks(bt, 0, 0, 0, configs);

        std::printf("  %s, %llu boards, 60 games per row\n", inst.describe().c_str(),
                    static_cast<unsigned long long>(configs.size()));
        std::printf("  %3s %8s %9s %11s %12s %10s %8s\n", "k", "turns", "split",
                    "peak union", "sweeps/game", "vs classic", "capped");
        for (int kk = 1; kk <= 5; ++kk) {
            const SalvoStats st = salvoRun(inst, configs, kk, 60, 20000);
            std::printf("  %3d %8.1f %9.1f %11llu %12.1f %10.1f %8d\n", kk, st.turns,
                        st.splits, static_cast<unsigned long long>(st.peakUnion),
                        st.sweeps, st.sweeps / st.shots, st.capped);
            std::fflush(stdout);
        }
        std::printf("\n");
    }

    std::printf("  k=1 is one sweep per shot by definition. k=2 costs a few times that and\n");
    std::printf("  survives. Past that the per-turn fan-out is C(k,h), which peaks at the\n");
    std::printf("  middle of the row, and it multiplies across turns.\n\n");
    std::printf("  The union stays well under the product of the fan-outs because most\n");
    std::printf("  assignments contradict the fleet within a turn or two, which is the only\n");
    std::printf("  reason k=2 is affordable. Nothing in the profile state merges branches:\n");
    std::printf("  two of them disagree about cells the sweep has already passed, so the\n");
    std::printf("  boundary carries no record of the disagreement.\n\n");
    std::printf("  Real salvo fires one shot per surviving ship, so it opens at k=5 and\n");
    std::printf("  shortens as ships sink. The k=5 row prices the opening.\n\n");
}

}  // namespace mayflower::m9
