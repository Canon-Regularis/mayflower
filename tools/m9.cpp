// m9: the mathematical extensions that reuse the engine directly.
//
//   1. The adaptive adversary. Expected shots assume the board was fixed before
//      play. Against a hider who never commits and answers to hurt most, the
//      same searcher faces a worst case instead.
//
//   2. Constraint density. Feed both a counting sweep and a backtracking search
//      observation records that no board produced, and sweep how constrained
//      they are. The search shows the easy-hard-easy profile of random
//      satisfiability. The sweep does not, and the reason is structural.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <set>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

#include "mayflower/certify.hpp"
#include "mayflower/constants.hpp"
#include "mayflower/exact_solver.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

using namespace mayflower;

inline int popcount64(std::uint64_t x) {
    return __builtin_popcountll(x);
}

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

// A backtracking feasibility search over the same records, for contrast.
//
// The DP is a counting sweep: it visits every layer once whatever the record
// says, so its cost tracks how many boundary states survive and nothing else.
// A search that places ships one at a time and abandons dead branches has the
// opposite profile, so running both on identical records separates the two.
struct Backtracker {
    const Instance& inst;
    const std::vector<CellConstraint>& cells;
    std::vector<std::vector<std::uint64_t>> options;  // per fleet slot
    std::uint64_t hitMask = 0;
    std::uint64_t nodes = 0;
    std::uint64_t cap;

    Backtracker(const Instance& i, const std::vector<CellConstraint>& c, std::uint64_t nodeCap)
        : inst(i), cells(c), cap(nodeCap) {
        std::uint64_t banned = 0;
        for (int k = 0; k < inst.cellCount(); ++k) {
            if (cells[static_cast<std::size_t>(k)] == CellConstraint::MustBeEmpty)
                banned |= 1ull << k;
            if (cells[static_cast<std::size_t>(k)] == CellConstraint::MustBeOccupied)
                hitMask |= 1ull << k;
        }
        for (int L : inst.fleet) {
            std::vector<std::uint64_t> masks;
            for (int r = 0; r < inst.height; ++r)
                for (int c2 = 0; c2 + L <= inst.width; ++c2) {
                    std::uint64_t m = 0;
                    for (int t = 0; t < L; ++t) m |= 1ull << inst.cellIndex(r, c2 + t);
                    if (!(m & banned)) masks.push_back(m);
                }
            if (L > 1)
                for (int r = 0; r + L <= inst.height; ++r)
                    for (int c2 = 0; c2 < inst.width; ++c2) {
                        std::uint64_t m = 0;
                        for (int t = 0; t < L; ++t) m |= 1ull << inst.cellIndex(r + t, c2);
                        if (!(m & banned)) masks.push_back(m);
                    }
            options.push_back(std::move(masks));
        }
    }

    // Slots hold the fleet in non-increasing length order, so equal lengths sit
    // next to each other and forcing their placement indices to increase kills
    // the duplicate branches without changing feasibility.
    bool search(std::size_t slot, std::uint64_t used, std::size_t from, int remainingCells) {
        if (nodes >= cap) return false;
        if (slot == options.size()) return (hitMask & ~used) == 0;

        // Every hit still uncovered has to be paid for by a later ship.
        if (popcount64(hitMask & ~used) > remainingCells) return false;

        const bool sameAsPrevious =
            slot > 0 && inst.fleet[slot] == inst.fleet[slot - 1];
        const auto& masks = options[slot];
        for (std::size_t i = sameAsPrevious ? from : 0; i < masks.size(); ++i) {
            if (masks[i] & used) continue;
            ++nodes;
            if (nodes >= cap) return false;
            if (search(slot + 1, used | masks[i], i + 1,
                       remainingCells - inst.fleet[slot]))
                return true;
        }
        return false;
    }

    bool solve() {
        return search(0, 0, 0, inst.shipCells());
    }
};

// 3. The adaptivity gap.
//
// A non-adaptive player commits to one order of the cells before play and reads
// nothing back. The game still ends when the last ship cell is shot, so for a
// fixed order the clearing time is the position of the board's last occupied
// cell, and
//
//   E[T] = sum_{t=0}^{n-1} P(the first t cells do not cover the board)
//        = n - (1/N) sum_{t=0}^{n-1} c(S_t),
//
// with S_t the first t cells and c(S) the number of configurations inside S.
// Every term depends on the prefix as a set, so the best order is the best chain
// through the subset lattice, and that is a DP over 2^n states rather than a
// search over n! orders. The cell-covering objective makes this the full-cover
// variant of min-sum set cover.
//
// c(S) for all S at once is a subset-sum transform over the configuration masks.
struct NonAdaptive {
    double optimal = 0;
    double greedy = 0;
    std::uint64_t configurations = 0;
};

void enumerateMasks(const Backtracker& bt, std::size_t slot, std::uint64_t used,
                    std::size_t from, std::vector<std::uint64_t>& out) {
    if (slot == bt.options.size()) { out.push_back(used); return; }
    const bool sameAsPrevious =
        slot > 0 && bt.inst.fleet[slot] == bt.inst.fleet[slot - 1];
    const auto& masks = bt.options[slot];
    for (std::size_t i = sameAsPrevious ? from : 0; i < masks.size(); ++i) {
        if (masks[i] & used) continue;
        enumerateMasks(bt, slot + 1, used | masks[i], i + 1, out);
    }
}

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

constexpr std::uint64_t kNodeCap = 20'000'000ull;

void sweepDensity(const Instance& inst, int shots, int maxHits, int step, int samples) {
    std::printf("  %s, %d cells shot, %d records per point\n", inst.describe().c_str(),
                shots, samples);
    std::printf("  %6s %10s %14s %11s %12s %12s %10s\n", "hits", "feasible",
                "median |Omega|", "DP mean us", "DP states", "search nodes", "capped");

    int dpPeakAt = 0, btPeakAt = 0;
    double dpPeak = 0, btPeak = 0;
    for (int hits = 0; hits <= maxHits; hits += step) {
        int feasible = 0, capped = 0;
        double totalUs = 0, totalNodes = 0;
        std::uint64_t peakStates = 0;
        std::vector<double> omegas;
        Rng rng(0xBEEF0000u + static_cast<std::uint64_t>(hits));

        for (int t = 0; t < samples; ++t) {
            std::vector<int> pool(static_cast<std::size_t>(inst.cellCount()));
            for (int i = 0; i < inst.cellCount(); ++i) pool[static_cast<std::size_t>(i)] = i;
            for (int i = inst.cellCount() - 1; i > 0; --i)
                std::swap(pool[static_cast<std::size_t>(i)],
                          pool[static_cast<std::size_t>(rng.below(i + 1))]);

            std::vector<CellConstraint> cells(static_cast<std::size_t>(inst.cellCount()),
                                              CellConstraint::Free);
            for (int i = 0; i < shots; ++i)
                cells[static_cast<std::size_t>(pool[static_cast<std::size_t>(i)])] =
                    i < hits ? CellConstraint::MustBeOccupied : CellConstraint::MustBeEmpty;

            const auto t0 = std::chrono::steady_clock::now();
            const auto r = countConfigurations(inst, cells);
            totalUs +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1e6;
            peakStates = std::max(peakStates, static_cast<std::uint64_t>(r.peakStates));
            if (r.count > 0) { ++feasible; omegas.push_back(static_cast<double>(r.count)); }

            Backtracker bt(inst, cells, kNodeCap);
            const bool found = bt.solve();
            const bool ranOut = bt.nodes >= kNodeCap;
            if (ranOut) ++capped;
            totalNodes += static_cast<double>(bt.nodes);
            // The two engines must agree on the yes/no answer. A search that hit
            // the cap returned no answer at all, so it is exempt.
            if (!ranOut && found != (r.count > 0)) {
                std::printf("  *** search and DP disagree at hits=%d, sample %d ***\n", hits, t);
                std::exit(1);
            }
        }
        std::sort(omegas.begin(), omegas.end());
        const double med = omegas.empty() ? 0 : omegas[omegas.size() / 2];
        const double meanUs = totalUs / samples;
        const double meanNodes = totalNodes / samples;
        if (meanUs > dpPeak)    { dpPeak = meanUs;    dpPeakAt = hits; }
        if (meanNodes > btPeak) { btPeak = meanNodes; btPeakAt = hits; }
        std::printf("  %6d %9.1f%% %14.4g %11.0f %12llu %12.0f %10d\n", hits,
                    100.0 * feasible / samples, med, meanUs,
                    static_cast<unsigned long long>(peakStates), meanNodes, capped);
        std::fflush(stdout);
    }
    std::printf("  DP cost peaks at %d hits, search cost peaks at %d hits\n\n",
                dpPeakAt, btPeakAt);
}

void phaseTransition() {
    std::printf("2. Constraint density\n");
    std::printf("---------------------\n\n");
    std::printf("Records here are synthetic: cells are chosen at random and a fraction of\n");
    std::printf("them declared hits, with no board behind the choice. Sweeping that fraction\n");
    std::printf("carries the record from easily satisfiable to plainly impossible, and the\n");
    std::printf("cost of deciding which peaks in between.\n\n");

    sweepDensity(Instance(6, 6, {4, 3, 2}), 18, 12, 1, 400);
    sweepDensity(Instance(7, 7, {5, 4, 3, 2}), 24, 16, 2, 120);
    sweepDensity(Instance(8, 8, {5, 4, 3, 3, 2}), 34, 20, 2, 40);

    std::printf("  Below the threshold nearly every record is satisfiable and the sweep\n");
    std::printf("  carries a wide state set to the end. Above it nearly none are, and the\n");
    std::printf("  sweep dies early. The expensive records sit at the edge, which is the\n");
    std::printf("  easy-hard-easy shape random satisfiability shows.\n");
}

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
    Rng rng(0x5A1F0ull + static_cast<std::uint64_t>(inst.cellCount() * 8 + k));

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

// 6. Noisy Battleship.
//
// Replace the truthful answer with a binary symmetric channel: every shot
// reports the cell's occupancy flipped with probability eps. A configuration's
// likelihood after t shots is (1-eps)^(t-m) eps^m with m the number of answers
// it disagrees with, so
//
//   P(B | O)  proportional to  exp(-beta m),   beta = ln((1-eps)/eps),
//
// which is a Boltzmann distribution over the mismatch count at inverse
// temperature beta. eps -> 0 sends beta -> infinity and recovers the hard
// filter: every configuration with a single mismatch is frozen out.
//
// Each shot is one use of a channel of capacity 1 - H(eps) bits, so identifying
// a board of H0 bits needs at least H0 / (1 - H(eps)) shots. Both sides are
// measured below.
double binaryEntropy(double p) {
    if (p <= 0 || p >= 1) return 0;
    return -p * std::log2(p) - (1 - p) * std::log2(1 - p);
}

void noisy() {
    std::printf("6. Noisy Battleship\n");
    std::printf("-------------------\n\n");
    std::printf("Every answer is flipped with probability eps. The likelihood of a board\n");
    std::printf("after t shots is (1-eps)^(t-m) eps^m in its mismatch count m, so the\n");
    std::printf("posterior is exp(-beta m) with beta = ln((1-eps)/eps). Noise is a\n");
    std::printf("temperature and the truthful game is the zero-temperature limit.\n\n");
    std::printf("Shots pick a uniform cell with replacement, so each one is a single use of\n");
    std::printf("a binary symmetric channel and the capacity bound applies directly.\n\n");

    struct C { int w, h; std::vector<int> f; int trials; };
    for (const C& k : std::vector<C>{{4,4,{3,2},400},{5,5,{4,3,2},60}}) {
        const Instance inst(k.w, k.h, k.f);
        const std::vector<CellConstraint> blank(static_cast<std::size_t>(inst.cellCount()),
                                                CellConstraint::Free);
        const Backtracker bt(inst, blank, ~0ull);
        std::vector<std::uint64_t> configs;
        enumerateMasks(bt, 0, 0, 0, configs);
        const std::size_t n = configs.size();
        const double h0 = std::log2(static_cast<double>(n));

        std::printf("  %s, %llu boards, H0 = %.4f bits, %d trials per row\n",
                    inst.describe().c_str(), static_cast<unsigned long long>(n), h0, k.trials);
        std::printf("  %7s %8s %10s %11s %10s %8s\n", "eps", "beta", "capacity",
                    "shots used", "bound", "ratio");

        for (double eps : {0.0005, 0.01, 0.05, 0.10, 0.20, 0.30}) {
            const double capacity = 1.0 - binaryEntropy(eps);
            const double beta = std::log((1 - eps) / eps);
            const int cap = 4000;
            double totalShots = 0;
            int finished = 0;
            Rng rng(0x0FF1CEull + static_cast<std::uint64_t>(eps * 1e6));

            for (int t = 0; t < k.trials; ++t) {
                const std::size_t truth = static_cast<std::size_t>(
                    rng.below(static_cast<int>(n)));
                std::vector<double> w(n, 1.0 / static_cast<double>(n));
                int shots = 0;
                double entropy = h0;

                while (entropy > 0.1 && shots < cap) {
                    const int cell = rng.below(inst.cellCount());
                    const bool occupied = (configs[truth] >> cell) & 1ull;
                    const bool flip = (static_cast<double>(rng.next() >> 11) /
                                       9007199254740992.0) < eps;
                    const bool answer = occupied != flip;
                    ++shots;

                    double sum = 0;
                    for (std::size_t i = 0; i < n; ++i) {
                        const bool oi = (configs[i] >> cell) & 1ull;
                        w[i] *= (oi == answer) ? (1 - eps) : eps;
                        sum += w[i];
                    }
                    entropy = 0;
                    for (std::size_t i = 0; i < n; ++i) {
                        w[i] /= sum;
                        if (w[i] > 0) entropy -= w[i] * std::log2(w[i]);
                    }
                }
                if (entropy <= 0.1) { totalShots += shots; ++finished; }
            }

            const double mean = finished ? totalShots / finished : 0;
            const double bound = capacity > 0 ? h0 / capacity : 0;
            std::printf("  %7.4f %8.2f %10.4f %11.1f %10.1f %8.2f\n", eps, beta, capacity,
                        mean, bound, mean / bound);
            std::fflush(stdout);
        }
        std::printf("\n");
    }

    // The Boltzmann form is an identity, so it can be checked rather than argued.
    {
        const Instance inst(4, 4, {3, 2});
        const std::vector<CellConstraint> blank(16, CellConstraint::Free);
        const Backtracker bt(inst, blank, ~0ull);
        std::vector<std::uint64_t> configs;
        enumerateMasks(bt, 0, 0, 0, configs);
        const double eps = 0.13;
        const double beta = std::log((1 - eps) / eps);
        Rng rng(99);

        const std::size_t truth = static_cast<std::size_t>(rng.below(static_cast<int>(configs.size())));
        std::vector<double> w(configs.size(), 1.0);
        std::vector<int> mismatch(configs.size(), 0);
        for (int t = 0; t < 25; ++t) {
            const int cell = rng.below(16);
            const bool answer = ((configs[truth] >> cell) & 1ull) != 0;
            for (std::size_t i = 0; i < configs.size(); ++i) {
                const bool oi = (configs[i] >> cell) & 1ull;
                if (oi == answer) w[i] *= (1 - eps);
                else { w[i] *= eps; ++mismatch[i]; }
            }
        }
        double worst = 0, za = 0, zb = 0;
        for (std::size_t i = 0; i < configs.size(); ++i) {
            za += w[i];
            zb += std::exp(-beta * mismatch[i]);
        }
        for (std::size_t i = 0; i < configs.size(); ++i)
            worst = std::max(worst, std::abs(w[i] / za - std::exp(-beta * mismatch[i]) / zb));
        std::printf("  Boltzmann identity, 4x4 {3,2} after 25 noisy shots at eps=0.13:\n");
        std::printf("  largest posterior difference between the likelihood product and\n");
        std::printf("  exp(-beta m)/Z is %.3e.\n\n", worst);
    }

    std::printf("  Shots used sits above the capacity bound at every noise level, which is\n");
    std::printf("  expected: a uniform random cell is not a capacity-achieving input. The\n");
    std::printf("  ratio is the interesting column, since it separates what the channel\n");
    std::printf("  costs from what the shot choice costs.\n\n");
    std::printf("  Scaling this to 10x10 needs a weighted sweep, which the engine does not\n");
    std::printf("  have. The counting path carries uint64 exact counts throughout, and a\n");
    std::printf("  noisy posterior needs floating-point weights on the transitions, so it\n");
    std::printf("  is a second pass rather than a flag. Everything above is exact by\n");
    std::printf("  enumeration and stops where enumeration stops.\n\n");
}

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

}  // namespace

int main(int argc, char** argv) {
    // No argument runs everything. A name runs one section, since the belief MDP
    // and the density sweep are minutes apart in cost.
    const std::string only = argc > 1 ? argv[1] : "";
    const bool all = only.empty();
    if (all) {
        std::printf("Mayflower, mathematical extensions\n");
        std::printf("==================================\n\n");
    }
    if (all || only == "adversary")  adversary();
    if (all || only == "density")    phaseTransition();
    if (all) std::printf("\n");
    if (all || only == "adaptivity") adaptivityGap();
    if (all || only == "bimaru")     bimaruCost();
    if (all || only == "salvo")      salvo();
    if (all || only == "noisy")      noisy();
    if (only == "selftest")          return selfTest();
    return 0;
}
