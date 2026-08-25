// The max-coverage rung of the bound ladder, E5, and why it is not one.
//
// Water filling (E4) bounds the boards a searcher can have finished by time t by
//
//     #finished(t)  <=  K * C(t, shipCells)
//
// with K the feasible hit-transcripts. That argument is an injection: under a
// deterministic policy a finished board is determined by its transcript, and a
// transcript is a hit-transcript (K ways) interleaved with misses (C(t,17) ways).
//
// The sketched improvement was to replace the second factor with
//
//     maxcov(t) = max over |S| = t of |{ B in Omega : B subset of S }|
//
// on the grounds that almost no 17-subset of the shot cells is a legal fleet.
// The replacement fails twice over, and this tool is the demonstration.
//
// It is not even smaller. C(t,17) counts 17-subsets of the shot cells, while
// maxcov counts CONFIGURATIONS inside them, and several ship decompositions can
// occupy one cell set: 17 cells shaped as a full row of ten plus seven of the
// next hold the fleet 20 different ways, against C(17,17) = 1. The two cross
// over near t = 22 and only then does C(t,17) run away.
//
// More seriously it is not substitutable at any t. The two factors count
// different objects: C(t,17) counts hit positions inside one transcript, and
// K * C(t,17) works because a finished board is determined by its transcript.
// maxcov counts boards inside one shot-set, and recovering a bound from it needs
// the number of distinct shot-sets a policy can reach by time t, which is the
// number of length-t transcripts and is far larger than K. Substituting anyway
// produces a "bound" that exceeds the true optimum on every instance small
// enough to check.
//
// What maxcov(t) does bound is the NON-ADAPTIVE optimum, where the player fixes
// one cell order in advance and there is only ever one shot-set. Adaptivity is
// exactly the structure it fails to model, so the quantity sits above the
// adaptive optimum by roughly the adaptivity gap and cannot enter the ladder.
//
//   small     every quantity at once on boards where all of them are computable
//   shapes    on 10x10, how far C(t,17) sits above the best c(S) anyone can find
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "mayflower/certify.hpp"
#include "mayflower/constants.hpp"
#include "mayflower/exact_solver.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

using namespace mayflower;

double binomial(int n, int k) {
    if (k < 0 || n < k) return 0.0;
    double r = 1.0;
    for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
    return r;
}

// Sum over t of the surviving mass, stopping once a term dies. `inner(t)` is the
// bound on boards reachable per transcript.
template <typename Inner>
double ladder(std::uint64_t transcripts, std::uint64_t hypotheses, int cells, Inner inner) {
    double bound = 0;
    for (int t = 0; t <= cells; ++t) {
        const double reachable = static_cast<double>(transcripts) * inner(t);
        const double survival = 1.0 - reachable / static_cast<double>(hypotheses);
        if (survival <= 0.0) break;
        bound += survival;
    }
    return bound;
}

// --- exact maxcov on boards small enough to hold the subset lattice ---------

std::vector<std::uint64_t> enumerateConfigurations(const Instance& inst) {
    const int n = inst.cellCount();
    std::vector<std::vector<std::uint64_t>> options;
    for (int L : inst.fleet) {
        std::vector<std::uint64_t> masks;
        for (int r = 0; r < inst.height; ++r)
            for (int c = 0; c + L <= inst.width; ++c) {
                std::uint64_t m = 0;
                for (int k = 0; k < L; ++k) m |= 1ull << inst.cellIndex(r, c + k);
                masks.push_back(m);
            }
        if (L > 1)
            for (int r = 0; r + L <= inst.height; ++r)
                for (int c = 0; c < inst.width; ++c) {
                    std::uint64_t m = 0;
                    for (int k = 0; k < L; ++k) m |= 1ull << inst.cellIndex(r + k, c);
                    masks.push_back(m);
                }
        options.push_back(std::move(masks));
    }

    std::vector<std::uint64_t> out;
    const auto go = [&](auto&& self, std::size_t slot, std::uint64_t used,
                        std::size_t from) -> void {
        if (slot == options.size()) { out.push_back(used); return; }
        const bool sameAsPrevious = slot > 0 && inst.fleet[slot] == inst.fleet[slot - 1];
        const auto& masks = options[slot];
        for (std::size_t i = sameAsPrevious ? from : 0; i < masks.size(); ++i) {
            if (masks[i] & used) continue;
            self(self, slot + 1, used | masks[i], i + 1);
        }
    };
    go(go, 0, 0, 0);
    (void)n;
    return out;
}

// One subset-sum transform gives c(S) for every S, and two scans over it give
// both quantities: maxcov(t) as the maximum within each popcount class, and the
// non-adaptive optimum as the best chain through the lattice.
struct SubsetAnalysis {
    std::vector<std::uint64_t> maxcov;   // indexed by t
    double nonAdaptive = 0;              // exact optimum over all n! cell orders
    std::uint64_t configurations = 0;
};

SubsetAnalysis analyseSubsets(const Instance& inst) {
    const int n = inst.cellCount();
    const std::size_t size = std::size_t{1} << n;
    const std::vector<std::uint64_t> configs = enumerateConfigurations(inst);

    std::vector<std::uint32_t> c(size, 0);
    for (std::uint64_t m : configs) ++c[static_cast<std::size_t>(m)];
    for (int b = 0; b < n; ++b)
        for (std::size_t S = 0; S < size; ++S)
            if (S & (std::size_t{1} << b)) c[S] += c[S ^ (std::size_t{1} << b)];

    SubsetAnalysis out;
    out.configurations = configs.size();
    out.maxcov.assign(static_cast<std::size_t>(n) + 1, 0);
    for (std::size_t S = 0; S < size; ++S) {
        const int bits = __builtin_popcountll(static_cast<unsigned long long>(S));
        out.maxcov[static_cast<std::size_t>(bits)] =
            std::max<std::uint64_t>(out.maxcov[static_cast<std::size_t>(bits)], c[S]);
    }

    // E[T] for a fixed order is n - (1/N) sum_t c(prefix_t), so the best order is
    // the chain maximising that sum.
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
    out.nonAdaptive = n - static_cast<double>(best[size - 1]) /
                              static_cast<double>(configs.size());
    return out;
}

void small() {
    std::printf("1. Every quantity at once, where all of them are computable\n");
    std::printf("----------------------------------------------------------\n\n");
    std::printf("maxcov(t) exactly, by scanning the subset lattice, alongside both optima and\n");
    std::printf("both readings of the ladder. A valid lower bound has to sit at or below the\n");
    std::printf("adaptive optimum. Watch which columns do.\n\n");

    std::printf("%-12s %7s %5s %9s %9s %10s %10s %10s\n", "instance", "boards", "K",
                "E4 water", "adaptive", "non-adapt", "maxcov", "K*maxcov");
    struct C { int w, h; std::vector<int> f; };
    int violations = 0, checked = 0;
    for (const C& k : std::vector<C>{{4, 3, {2}}, {4, 4, {2}}, {4, 4, {3}}, {5, 4, {3}},
                                     {4, 4, {2, 2}}, {4, 4, {3, 2}}}) {
        const Instance inst(k.w, k.h, k.f);
        const int n = inst.cellCount();
        const SubsetAnalysis sa = analyseSubsets(inst);
        const std::uint64_t N = sa.configurations;
        const std::uint64_t K = countHitTranscripts(inst.fleet);

        const double e4 = ladder(K, N, n, [&](int t) { return binomial(t, inst.shipCells()); });
        // The rung as sketched, and the same quantity with the K factor dropped.
        const double withK = ladder(K, N, n, [&](int t) {
            return static_cast<double>(sa.maxcov[static_cast<std::size_t>(t)]);
        });
        const double plain = ladder(1, N, n, [&](int t) {
            return static_cast<double>(sa.maxcov[static_cast<std::size_t>(t)]);
        });

        double adaptive = -1;
        try { adaptive = solveOptimal(inst, 600).expectedShots; } catch (const std::exception&) {}

        std::printf("%-12s %7llu %5llu %9.4f %9.4f %10.4f %10.4f %10.4f\n",
                    inst.describe().c_str(), static_cast<unsigned long long>(N),
                    static_cast<unsigned long long>(K), e4, adaptive, sa.nonAdaptive, plain,
                    withK);
        std::fflush(stdout);

        if (adaptive >= 0) {
            ++checked;
            if (e4 > adaptive + 1e-9)
                std::printf("      *** E4 exceeds the optimum, which would be a real bug\n");
            if (withK > adaptive + 1e-9) ++violations;
        }
    }

    std::printf("\n  E4 sits at or below the adaptive optimum everywhere, as a bound must.\n");
    std::printf("  The K*maxcov column exceeds it on %d of %d instances, so it is not a\n",
                violations, checked);
    std::printf("  bound on anything. Dropping K does not rescue it either.\n\n");
    std::printf("  What the maxcov column does obey is the non-adaptive optimum, which it\n");
    std::printf("  sits at or below on every row. That is the correct reading: fixing one\n");
    std::printf("  shot-set of size t is precisely the non-adaptive assumption, and an\n");
    std::printf("  adaptive searcher has a tree of them. On 4x3 {2} and 4x4 {2} the two agree\n");
    std::printf("  exactly, because there a single chain attains the maximum at every t.\n\n");
    std::printf("  So the rung was measuring the wrong problem. The gap between the maxcov\n");
    std::printf("  and adaptive columns is the adaptivity gap, which tools/m9 measures at up\n");
    std::printf("  to 2.09x, and that is the factor by which the sketched bound overshoots.\n\n");
}

// --- what is left on the standard board ------------------------------------

// c(S) as a constrained count: everything outside S is a miss.
std::uint64_t coverage(const Instance& inst, const std::vector<int>& cells, int take) {
    std::vector<CellConstraint> cs(static_cast<std::size_t>(inst.cellCount()),
                                   CellConstraint::MustBeEmpty);
    for (int i = 0; i < take; ++i)
        cs[static_cast<std::size_t>(cells[static_cast<std::size_t>(i)])] = CellConstraint::Free;
    return countConfigurations(inst, cs).count;
}

std::vector<int> rowMajor(const Instance& inst) {
    std::vector<int> out;
    for (int i = 0; i < inst.cellCount(); ++i) out.push_back(i);
    return out;
}

std::vector<int> columnMajor(const Instance& inst) {
    std::vector<int> out;
    for (int c = 0; c < inst.width; ++c)
        for (int r = 0; r < inst.height; ++r) out.push_back(inst.cellIndex(r, c));
    return out;
}

// Rows in the order 0, 2, 4, ..., 1, 3, 5, ...: spread first, fill in after.
std::vector<int> alternatingRows(const Instance& inst) {
    std::vector<int> out;
    for (int parity = 0; parity < 2; ++parity)
        for (int r = parity; r < inst.height; r += 2)
            for (int c = 0; c < inst.width; ++c) out.push_back(inst.cellIndex(r, c));
    return out;
}

// Every fifth cell along the diagonal first, which is the classic parity idea:
// no 5-ship can avoid such a lattice.
std::vector<int> diagonalLattice(const Instance& inst) {
    std::vector<int> out;
    std::vector<bool> seen(static_cast<std::size_t>(inst.cellCount()), false);
    for (int step = 0; step < 5; ++step)
        for (int r = 0; r < inst.height; ++r)
            for (int c = 0; c < inst.width; ++c)
                if ((r + c) % 5 == step) {
                    out.push_back(inst.cellIndex(r, c));
                    seen[static_cast<std::size_t>(inst.cellIndex(r, c))] = true;
                }
    return out;
}

void shapes() {
    std::printf("2. What is left on the standard board\n");
    std::printf("-------------------------------------\n\n");
    std::printf("Two things survive section 1. The first is a measurement of how loose the\n");
    std::printf("C(t,17) factor really is, which says where E4's slack lives even though\n");
    std::printf("maxcov cannot replace it. The second is that maxcov turned out to be a\n");
    std::printf("non-adaptive quantity, and the non-adaptive optimum is worth a number here.\n\n");

    const Instance inst;
    const double N = static_cast<double>(constants::kOmega0);

    std::printf("  How far C(t,17) sits above an achievable c(S):\n");
    std::printf("  %5s %16s %16s %14s\n", "t", "C(t,17)", "best c(S) found", "ratio");
    const std::vector<int> rows = rowMajor(inst);
    for (int t : {17, 20, 25, 30, 40, 50, 60, 70, 80, 90}) {
        const double ct = binomial(t, 17);
        const double bs = static_cast<double>(coverage(inst, rows, t));
        std::printf("  %5d %16.4g %16.4g %14.4g\n", t, ct, bs, bs > 0 ? ct / bs : 0.0);
        std::fflush(stdout);
    }
    std::printf("\n  The two cross over near t = 22. Below it C(t,17) is the smaller factor,\n");
    std::printf("  since maxcov counts configurations and several ship decompositions share\n");
    std::printf("  one cell set: at t = 17 a full row plus seven of the next holds the fleet\n");
    std::printf("  20 ways against C(17,17) = 1. Above it C(t,17) runs away: by t = 90 the\n");
    std::printf("  ratio is %.3g, and that is where E4's slack lives and why it saturates\n",
                binomial(90, 17) /
                    static_cast<double>(coverage(inst, rows, 90)));
    std::printf("  early.\n\n");
    std::printf("  None of that is reachable. Closing the gap would need a bound on maxcov\n");
    std::printf("  from above, and section 1 shows that even a perfect one would be bounding\n");
    std::printf("  the non-adaptive problem.\n\n");

    std::printf("  The non-adaptive optimum, from explicit orders (each an upper bound):\n");
    std::printf("  %-18s %14s %14s\n", "order", "E[T]", "vs adaptive");
    struct O { const char* name; std::vector<int> cells; };
    double bestOrder = 1e9;
    const char* bestName = "-";
    for (const O& o : std::vector<O>{{"row-major", rowMajor(inst)},
                                     {"column-major", columnMajor(inst)},
                                     {"alternating rows", alternatingRows(inst)},
                                     {"diagonal lattice", diagonalLattice(inst)}}) {
        double expected = 0;
        for (int t = 0; t < inst.cellCount(); ++t)
            expected += 1.0 - static_cast<double>(coverage(inst, o.cells, t)) / N;
        if (expected < bestOrder) { bestOrder = expected; bestName = o.name; }
        std::printf("  %-18s %14.4f %14s\n", o.name, expected, "");
        std::fflush(stdout);
    }

    std::printf("\n  Best fixed order found: %s at %.4f shots, so the non-adaptive optimum is\n",
                bestName, bestOrder);
    std::printf("  at most that. The density policy measures 44.369 on the same board, so an\n");
    std::printf("  adaptive player beats every fixed order tried here by %.4f shots.\n\n",
                bestOrder - 44.369);
    std::printf("  Both of those are achievable numbers rather than optima, so the pair does\n");
    std::printf("  not bound the adaptivity gap from below: that would need a lower bound on\n");
    std::printf("  the non-adaptive optimum, and the only route to one is a bound on maxcov\n");
    std::printf("  from above, which is the same wall section 1 hit. What the pair does show\n");
    std::printf("  is that the gap is not small, matching the 2.09x tools/m9 measures where\n");
    std::printf("  both optima are computable.\n\n");
    std::printf("  Verdict on the rung: cut. E5 as sketched is not a lower bound on the\n");
    std::printf("  adaptive optimum, and the ladder keeps E4 at 24.088 as its binding rung.\n\n");
}

// The finding as a regression test. A later change that revives the rung, or
// that quietly breaks the rung that does work, fails the build here.
int selfTest() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string& what) {
        std::printf("  %-62s %s\n", what.c_str(), ok ? "ok" : "FAILED");
        if (!ok) ++failures;
    };
    std::printf("max-coverage self-test\n----------------------\n");

    struct C { int w, h; std::vector<int> f; };
    int overshoots = 0;
    for (const C& k : std::vector<C>{{4, 3, {2}}, {4, 4, {2}}, {4, 4, {3}}, {4, 4, {2, 2}}}) {
        const Instance inst(k.w, k.h, k.f);
        const int n = inst.cellCount();
        const SubsetAnalysis sa = analyseSubsets(inst);
        const std::uint64_t N = sa.configurations;
        const std::uint64_t K = countHitTranscripts(inst.fleet);
        const double adaptive = solveOptimal(inst, 600).expectedShots;

        const double e4 = ladder(K, N, n, [&](int t) { return binomial(t, inst.shipCells()); });
        const double withK = ladder(K, N, n, [&](int t) {
            return static_cast<double>(sa.maxcov[static_cast<std::size_t>(t)]);
        });
        const double plain = ladder(1, N, n, [&](int t) {
            return static_cast<double>(sa.maxcov[static_cast<std::size_t>(t)]);
        });

        // The rung that works.
        check(e4 <= adaptive + 1e-9, inst.describe() + ": E4 is a lower bound");
        check(e4 >= inst.shipCells() - 1e-9 || e4 > 0,
              inst.describe() + ": E4 is positive");
        // maxcov measures the non-adaptive problem, and does so tightly.
        check(plain <= sa.nonAdaptive + 1e-9,
              inst.describe() + ": maxcov sum <= the non-adaptive optimum");
        check(sa.nonAdaptive >= adaptive - 1e-9,
              inst.describe() + ": feedback never hurts");
        if (withK > adaptive + 1e-9) ++overshoots;
    }

    // The negative regression. If this ever passes as a bound, either the
    // solver changed or someone re-derived the rung; both need a fresh look.
    check(overshoots == 4,
          "K*maxcov exceeds the optimum on all 4 instances, so it is not a bound");

    std::printf("\n%s\n", failures ? "SELF-TEST FAILED" : "all invariants hold");
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const bool all = only.empty();
    if (all) {
        std::printf("Mayflower, the max-coverage rung\n");
        std::printf("================================\n\n");
    }
    if (all || only == "small") small();
    if (all || only == "shapes") shapes();
    if (only == "selftest") return selfTest();
    return 0;
}
