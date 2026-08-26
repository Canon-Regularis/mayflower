// The weighted sweep at full scale: what soft evidence and a non-uniform prior
// do to the standard instance.
//
//   bridge      every weight 1 must return the integer count bit for bit
//   evidence    a noisy record has an exact normaliser, one sweep
//   marginals   the posterior heatmap under noise, forward and backward once
//   prior       a log-linear opponent model, folded in as placement weights
//
// Run a section by name, or all of them with no argument.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/weighted.hpp"

namespace {

using namespace mayflower;

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
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

// A fixed legal board, so every run below reads the same hidden truth.
std::vector<int> truthCells() {
    return {0,  1,  2,  3,  4,        // 5-ship, row 0
            9,  19, 29, 39,           // 4-ship, column 9
            52, 53, 54,               // 3-ship, row 5
            66, 76, 86,               // 3-ship, column 6
            90, 91};                  // 2-ship, row 9
}

std::vector<bool> truthMask() {
    std::vector<bool> occ(100, false);
    for (int c : truthCells()) occ[static_cast<std::size_t>(c)] = true;
    return occ;
}

// Shoot `shots` random cells and answer each through a symmetric channel.
std::vector<int> noisyRecord(int shots, double eps, std::uint64_t seed) {
    const std::vector<bool> occ = truthMask();
    Rng rng(seed);
    std::vector<int> order(100);
    for (int i = 0; i < 100; ++i) order[static_cast<std::size_t>(i)] = i;
    for (int i = 99; i > 0; --i)
        std::swap(order[static_cast<std::size_t>(i)],
                  order[static_cast<std::size_t>(rng.below(i + 1))]);

    std::vector<int> answers(100, -1);
    for (int i = 0; i < shots; ++i) {
        const int cell = order[static_cast<std::size_t>(i)];
        const bool truth = occ[static_cast<std::size_t>(cell)];
        const bool flip = rng.unit() < eps;
        answers[static_cast<std::size_t>(cell)] = (truth != flip) ? 1 : 0;
    }
    return answers;
}

void bridge() {
    std::printf("1. The unweighted bridge\n");
    std::printf("------------------------\n\n");
    std::printf("Weighting turns the count into a partition function, so integer exactness\n");
    std::printf("goes with it. What survives is the boundary case: with every weight at 1 the\n");
    std::printf("sum has to be the count, bit for bit, and it is, because every layer sum\n");
    std::printf("stays far below 2^53 and double addition of integers that size is exact.\n\n");

    const Instance inst;
    const auto t0 = std::chrono::steady_clock::now();
    const WeightedResult r = weightedCount(inst, Weights::uniform());
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::printf("  expected           %llu\n",
                static_cast<unsigned long long>(constants::kOmega0));
    std::printf("  weighted sum       %.1f\n", r.total);
    std::printf("  VERDICT            %s\n",
                r.total == static_cast<double>(constants::kOmega0) ? "EXACT" : "*** OFF ***");
    std::printf("  rescaled           %s\n", r.rescaled ? "yes" : "no");
    std::printf("  largest layer sum  %.6g  (2^%.2f, against 2^53)\n", r.maxLayerSum,
                std::log2(r.maxLayerSum));
    std::printf("  %.2f s, peak %zu states, %llu edges\n\n", seconds, r.peakStates,
                static_cast<unsigned long long>(r.edges));
}

void evidence() {
    std::printf("2. Evidence under a noisy channel\n");
    std::printf("---------------------------------\n\n");
    std::printf("Every answer is flipped with probability eps, so no answer forbids anything\n");
    std::printf("and the record filters nothing out. What it does instead is reweight, and\n");
    std::printf("one sweep returns the exact normaliser over all 15,046,987,768 boards.\n\n");

    const Instance inst;
    std::printf("  %6s %8s %18s %14s %10s\n", "eps", "shots", "log evidence", "vs prior",
                "seconds");
    for (double eps : {0.02, 0.05, 0.10, 0.20, 0.35, 0.50}) {
        for (int shots : {20, 40}) {
            const std::vector<int> answers = noisyRecord(shots, eps, 20260825ull);
            const Weights w = Weights::noisyChannel(inst, answers, eps);
            const auto t0 = std::chrono::steady_clock::now();
            const WeightedResult r = weightedCount(inst, w);
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            // Against the prior: how much mass the record kept, in bits.
            const double bits = (r.logTotal - std::log(static_cast<double>(constants::kOmega0))) /
                                std::log(2.0);
            std::printf("  %6.2f %8d %18.6f %13.2fb %10.2f\n", eps, shots, r.logTotal, bits,
                        seconds);
            std::fflush(stdout);
        }
    }
    std::printf("\n  The record never drives the evidence to zero, which is the whole\n");
    std::printf("  difference from the truthful game: a contradictory record is merely an\n");
    std::printf("  unlikely one.\n\n");
    std::printf("  Bits against the prior is log2 of the average likelihood over all\n");
    std::printf("  15,046,987,768 boards. It falls with the shot count, as more answers cost\n");
    std::printf("  the average board more. In eps it is not monotone: it bottoms out near\n");
    std::printf("  eps = 0.1 and climbs back afterwards, because a channel that noisy stops\n");
    std::printf("  punishing disagreement.\n\n");
    std::printf("  The eps = 0.5 row is the anchor. There every weight is exactly 0.5, so\n");
    std::printf("  every board has likelihood 2^-t whatever it looks like, the evidence is\n");
    std::printf("  |Omega| * 2^-t, and the column has to read exactly -20.00 and -40.00.\n");
    std::printf("  It does, which prices the rest of the column.\n\n");
}

void marginals() {
    std::printf("3. The posterior heatmap under noise\n");
    std::printf("------------------------------------\n\n");
    std::printf("One forward and one backward pass for all hundred cells. The empty\n");
    std::printf("transition maps a state to itself, so the weight passing through a cell\n");
    std::printf("without occupying it is a single sum, and the occupied weight is the total\n");
    std::printf("minus it. Two passes replace a hundred constrained sweeps, which is the\n");
    std::printf("claim. The measured saving was about twelve times on an idle machine and\n");
    std::printf("about five under load, so the timing below is not a constant.\n\n");

    const Instance inst;
    Constraints free;
    free.cells.assign(100, CellConstraint::Free);
    const std::vector<bool> occ = truthMask();

    for (double eps : {0.05, 0.20}) {
        const std::vector<int> answers = noisyRecord(30, eps, 20260825ull);
        const Weights w = Weights::noisyChannel(inst, answers, eps);

        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<double> m = weightedMarginals(inst, free, w);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        std::printf("  eps = %.2f, 30 shots, %.1f s for the whole board\n", eps, seconds);
        std::printf("     ");
        for (int c = 0; c < 10; ++c) std::printf("%7d", c);
        std::printf("\n");
        for (int r = 0; r < 10; ++r) {
            std::printf("  r%d ", r);
            for (int c = 0; c < 10; ++c) {
                const std::size_t cell = static_cast<std::size_t>(r * 10 + c);
                std::printf("%7.3f", m[cell]);
            }
            std::printf("\n");
        }

        // How well the posterior ranks the true ship cells.
        std::vector<std::pair<double, int>> ranked;
        for (int c = 0; c < 100; ++c)
            ranked.push_back({m[static_cast<std::size_t>(c)], c});
        std::sort(ranked.rbegin(), ranked.rend());
        int hitsInTop17 = 0;
        for (int i = 0; i < 17; ++i)
            if (occ[static_cast<std::size_t>(ranked[static_cast<std::size_t>(i)].second)])
                ++hitsInTop17;
        double sum = 0;
        for (double v : m) sum += v;
        std::printf("  marginals sum to %.6f, and %d of the top 17 cells are really ships\n\n",
                    sum, hitsInTop17);
        std::fflush(stdout);
    }
}

void prior() {
    std::printf("4. A log-linear opponent prior\n");
    std::printf("------------------------------\n\n");
    std::printf("Placement weights carry an opponent model. Two features here: how close a\n");
    std::printf("ship sits to the border, and whether it runs vertically. Setting both\n");
    std::printf("coefficients to zero returns the uniform prior exactly, which is the same\n");
    std::printf("bridge as section 1 and is checked below.\n\n");

    const Instance inst;
    const std::vector<int> lengths = inst.distinctLengths();
    const std::size_t nL = lengths.size();

    // Distance from a placement's origin to the nearest border, normalised.
    const auto build = [&](double thetaEdge, double thetaVertical) {
        std::vector<double> logH(100 * nL, 0.0), logV(100 * nL, 0.0);
        for (int r = 0; r < 10; ++r)
            for (int c = 0; c < 10; ++c) {
                const int cell = r * 10 + c;
                const double border =
                    std::min(std::min(r, 9 - r), std::min(c, 9 - c)) / 4.5;   // 0 edge, 1 centre
                for (std::size_t li = 0; li < nL; ++li) {
                    logH[static_cast<std::size_t>(cell) * nL + li] = -thetaEdge * border;
                    logV[static_cast<std::size_t>(cell) * nL + li] =
                        -thetaEdge * border + thetaVertical;
                }
            }
        return Weights::fromLogPlacementScores(inst, logH, logV);
    };

    const WeightedResult flat = weightedCount(inst, build(0.0, 0.0));
    std::printf("  theta = 0 gives %.1f against %llu, %s\n\n", flat.total,
                static_cast<unsigned long long>(constants::kOmega0),
                flat.total == static_cast<double>(constants::kOmega0) ? "exact" : "*** OFF ***");

    Constraints free;
    free.cells.assign(100, CellConstraint::Free);

    // Cells (0,4) and (4,0) are mirror images under the board's diagonal
    // symmetry, so a symmetric prior has to give them the same marginal and an
    // orientation bias has to split them. That makes the pair a test rather
    // than a pair of numbers.
    std::printf("  %8s %10s %14s %10s %10s %10s %10s\n", "edge", "vertical", "log Z",
                "corner", "centre", "m(0,4)", "m(4,0)");
    for (const auto& theta : std::vector<std::pair<double, double>>{
             {0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {0.0, 1.0}, {2.0, 1.0}}) {
        const Weights w = build(theta.first, theta.second);
        const WeightedResult z = weightedCount(inst, w);
        std::printf("  %8.1f %10.1f %14.6f %10.4f %10.4f %10.4f %10.4f\n", theta.first,
                    theta.second, z.logTotal, weightedMarginal(inst, free, w, 0),
                    weightedMarginal(inst, free, w, 55), weightedMarginal(inst, free, w, 4),
                    weightedMarginal(inst, free, w, 40));
        std::fflush(stdout);
    }

    std::printf("\n  The first row is a second bridge, and a sharper one than the total. Its\n");
    std::printf("  marginals are 0.0800 at the corner, 0.2136 at the centre and 0.1667 at the\n");
    std::printf("  edge midpoint, which is the exact prior table tools/marginals derives\n");
    std::printf("  through integer counts. The weighted path reaches the same numbers by a\n");
    std::printf("  different route.\n\n");
    std::printf("  Raising the edge coefficient moves mass off the centre and onto the\n");
    std::printf("  border, which is what a player who hugs the edges looks like to the\n");
    std::printf("  engine. By edge = 2 the ordering has inverted: the corner reads 0.1126\n");
    std::printf("  against the centre's 0.1075, where the uniform prior had the centre ahead\n");
    std::printf("  2.67 to 1.\n\n");
    std::printf("  The last two columns are a symmetry check. Cells (0,4) and (4,0) are\n");
    std::printf("  reflections of each other, so they must read the same whenever the prior\n");
    std::printf("  treats the two orientations alike, and must part once it does not.\n\n");
    std::printf("  The prior enters as placement weights and the observation channel enters\n");
    std::printf("  as cell weights, so a posterior under a non-uniform opponent is one sweep\n");
    std::printf("  with both sets supplied.\n\n");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const bool all = only.empty();
    if (all) {
        std::printf("Mayflower, weighted counting\n");
        std::printf("============================\n\n");
    }
    if (all || only == "bridge") bridge();
    if (all || only == "evidence") evidence();
    if (all || only == "prior") prior();
    if (all || only == "marginals") marginals();
    return 0;
}
