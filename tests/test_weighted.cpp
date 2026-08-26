// The weighted sweep, which is a partition function rather than a count.
//
// Integer exactness is gone, so this file establishes two things instead: that
// the unweighted case is still bit-exact, and that the weighted case agrees with
// literal enumeration to a stated tolerance.
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/weighted.hpp"
#include "oracle/brute_force.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %-64s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}

// splitmix64, so weights are reproducible from a seed alone and another
// implementation can be pointed at the same numbers.
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
};

// Draw order is fixed: all occupied weights, all empty weights, then the
// horizontal placement weights length-major, then the vertical ones.
mayflower::Weights drawWeights(const mayflower::Instance& inst, std::uint64_t seed) {
    const int n = inst.cellCount();
    const std::size_t nL = inst.distinctLengths().size();
    Rng rng(seed);
    mayflower::Weights w;
    w.occupied.resize(static_cast<std::size_t>(n));
    w.empty.resize(static_cast<std::size_t>(n));
    w.startH.assign(static_cast<std::size_t>(n) * nL, 1.0);
    w.startV.assign(static_cast<std::size_t>(n) * nL, 1.0);
    for (int c = 0; c < n; ++c) w.occupied[static_cast<std::size_t>(c)] = 0.5 + rng.unit();
    for (int c = 0; c < n; ++c) w.empty[static_cast<std::size_t>(c)] = 0.5 + rng.unit();
    for (std::size_t li = 0; li < nL; ++li)
        for (int c = 0; c < n; ++c)
            w.startH[static_cast<std::size_t>(c) * nL + li] = 0.5 + rng.unit();
    for (std::size_t li = 0; li < nL; ++li)
        for (int c = 0; c < n; ++c)
            w.startV[static_cast<std::size_t>(c) * nL + li] = 0.5 + rng.unit();
    return w;
}

// The same product, computed by walking every board.
double enumerateWeighted(const mayflower::Instance& inst, const mayflower::Weights& w) {
    const int W = inst.width, H = inst.height;
    const std::vector<int> lengths = inst.distinctLengths();
    const std::size_t nL = lengths.size();
    const auto boards = oracle::enumerateBoards(W, H, inst.fleet);

    double total = 0;
    for (const auto& ships : boards) {
        double weight = 1.0;
        oracle::Mask occupancy = 0;
        for (oracle::Mask ship : ships) {
            occupancy |= ship;
            int origin = -1, second = -1, length = 0;
            for (int c = 0; c < W * H; ++c)
                if (((ship >> c) & oracle::Mask{1}) != 0) {
                    if (origin < 0) origin = c;
                    else if (second < 0) second = c;
                    ++length;
                }
            const std::size_t li = static_cast<std::size_t>(
                std::find(lengths.begin(), lengths.end(), length) - lengths.begin());
            const bool horizontal = (second - origin) == 1;
            const std::vector<double>& table = horizontal ? w.startH : w.startV;
            if (!table.empty()) weight *= table[static_cast<std::size_t>(origin) * nL + li];
        }
        for (int c = 0; c < W * H; ++c) {
            const bool occ = ((occupancy >> c) & oracle::Mask{1}) != 0;
            if (occ && !w.occupied.empty()) weight *= w.occupied[static_cast<std::size_t>(c)];
            if (!occ && !w.empty.empty()) weight *= w.empty[static_cast<std::size_t>(c)];
        }
        total += weight;
    }
    return total;
}

double enumerateWeightedOccupied(const mayflower::Instance& inst,
                                 const mayflower::Weights& w, int cell) {
    const int W = inst.width, H = inst.height;
    const std::vector<int> lengths = inst.distinctLengths();
    const std::size_t nL = lengths.size();
    const auto boards = oracle::enumerateBoards(W, H, inst.fleet);

    double total = 0;
    for (const auto& ships : boards) {
        oracle::Mask occupancy = 0;
        for (oracle::Mask ship : ships) occupancy |= ship;
        if (((occupancy >> cell) & oracle::Mask{1}) == 0) continue;

        double weight = 1.0;
        for (oracle::Mask ship : ships) {
            int origin = -1, second = -1, length = 0;
            for (int c = 0; c < W * H; ++c)
                if (((ship >> c) & oracle::Mask{1}) != 0) {
                    if (origin < 0) origin = c;
                    else if (second < 0) second = c;
                    ++length;
                }
            const std::size_t li = static_cast<std::size_t>(
                std::find(lengths.begin(), lengths.end(), length) - lengths.begin());
            const std::vector<double>& table = (second - origin) == 1 ? w.startH : w.startV;
            if (!table.empty()) weight *= table[static_cast<std::size_t>(origin) * nL + li];
        }
        for (int c = 0; c < W * H; ++c) {
            const bool occ = ((occupancy >> c) & oracle::Mask{1}) != 0;
            if (occ && !w.occupied.empty()) weight *= w.occupied[static_cast<std::size_t>(c)];
            if (!occ && !w.empty.empty()) weight *= w.empty[static_cast<std::size_t>(c)];
        }
        total += weight;
    }
    return total;
}

bool close(double a, double b, double rel) {
    if (a == b) return true;
    const double scale = std::max(std::abs(a), std::abs(b));
    return std::abs(a - b) <= rel * scale;
}

// theta = 0 must return the integer count, not merely something near it.
void testUnweightedBridge() {
    std::printf("[the unweighted bridge]\n");
    struct C { int w, h; std::vector<int> f; };
    for (const C& c : std::vector<C>{{4, 4, {3, 2}}, {5, 5, {4, 3, 2}}, {6, 6, {4, 3, 2}},
                                     {5, 5, {3, 3, 2}}}) {
        const mayflower::Instance inst(c.w, c.h, c.f);
        const auto wr = mayflower::weightedCount(inst, mayflower::Weights::uniform());
        const std::uint64_t exact = mayflower::countConfigurations(inst).count;
        check(wr.total == static_cast<double>(exact) && !wr.rescaled,
              inst.describe() + ": weighted sum is the count, bit for bit");
    }

    const mayflower::Instance std10;
    const auto wr = mayflower::weightedCount(std10, mayflower::Weights::uniform());
    check(wr.total == static_cast<double>(mayflower::constants::kOmega0),
          "10x10 {5,4,3,3,2}: 15,046,987,768 exactly");
    check(!wr.rescaled, "and no layer needed rescaling");
    check(wr.exact, "and the run certifies itself exact");

    // A weighted run must never claim exactness.
    const auto tilted = mayflower::weightedCount(
        std10, mayflower::Weights::noisyChannel(
                   std10, std::vector<int>(100, -1), 0.1));
    check(!tilted.exact, "a weighted run does not claim to be exact");

    // The exactness argument rests on this staying below 2^53.
    const double limit = 9007199254740992.0;
    check(wr.maxLayerSum < limit, "largest layer sum stays below 2^53");
    std::printf("      largest layer sum %.6g, which is 2^%.2f, against 2^53\n",
                wr.maxLayerSum, std::log2(wr.maxLayerSum));
}

void testAgainstEnumeration() {
    std::printf("[weighted sums against enumeration]\n");
    struct C { int w, h; std::vector<int> f; std::uint64_t seed; };
    for (const C& c : std::vector<C>{{4, 4, {3, 2}, 12345}, {4, 4, {2, 2}, 777},
                                     {5, 5, {3, 2}, 999}, {4, 4, {4, 3}, 31337}}) {
        const mayflower::Instance inst(c.w, c.h, c.f);
        const mayflower::Weights w = drawWeights(inst, c.seed);
        const double sweep = mayflower::weightedCount(inst, w).total;
        const double brute = enumerateWeighted(inst, w);
        char label[128];
        std::snprintf(label, sizeof label, "%s seed %llu: %.12g", inst.describe().c_str(),
                      static_cast<unsigned long long>(c.seed), sweep);
        check(close(sweep, brute, 1e-12), label);
        if (!close(sweep, brute, 1e-12))
            std::printf("      sweep %.17g, enumeration %.17g, relative %.3e\n", sweep, brute,
                        std::abs(sweep - brute) / brute);
    }
}

// Placement weights alone reweight the prior and must leave the cell weights out
// of it, so this case isolates them.
void testPlacementWeightsOnly() {
    std::printf("[placement weights alone]\n");
    const mayflower::Instance inst(4, 4, {3, 2});
    mayflower::Weights w = drawWeights(inst, 4242);
    w.occupied.clear();
    w.empty.clear();
    const double sweep = mayflower::weightedCount(inst, w).total;
    const double brute = enumerateWeighted(inst, w);
    char label[128];
    std::snprintf(label, sizeof label, "4x4 {3,2} placements only: %.12g", sweep);
    check(close(sweep, brute, 1e-12), label);
}

// The noisy channel: soft evidence, no cell forbidden.
void testNoisyChannel() {
    std::printf("[a noisy observation record]\n");
    const mayflower::Instance inst(4, 4, {3, 2});
    std::vector<int> answers(16, -1);
    for (int c : {0, 3, 5, 9, 12}) answers[static_cast<std::size_t>(c)] = 1;
    for (int c : {1, 7, 10}) answers[static_cast<std::size_t>(c)] = 0;

    const mayflower::Weights w = mayflower::Weights::noisyChannel(inst, answers, 0.1);
    const double sweep = mayflower::weightedCount(inst, w).total;
    const double brute = enumerateWeighted(inst, w);
    char label[128];
    std::snprintf(label, sizeof label, "evidence at eps=0.1: %.12g", sweep);
    check(close(sweep, brute, 1e-12), label);

    const double marginal = mayflower::weightedMarginal(
        inst, mayflower::Constraints{std::vector<mayflower::CellConstraint>(
                  16, mayflower::CellConstraint::Free), {}, {}},
        w, 6);
    const double bruteMarginal = enumerateWeightedOccupied(inst, w, 6) / brute;
    std::snprintf(label, sizeof label, "posterior marginal of cell 6: %.12g", marginal);
    check(close(marginal, bruteMarginal, 1e-12), label);

    // That record is one no board satisfies. Read truthfully it is a
    // contradiction; read through a channel it is merely unlikely, and the
    // evidence stays positive. Noise removes infeasibility as a category.
    std::vector<mayflower::CellConstraint> hard(16, mayflower::CellConstraint::Free);
    for (int c : {0, 3, 5, 9, 12}) hard[static_cast<std::size_t>(c)] =
        mayflower::CellConstraint::MustBeOccupied;
    for (int c : {1, 7, 10}) hard[static_cast<std::size_t>(c)] =
        mayflower::CellConstraint::MustBeEmpty;
    check(mayflower::countConfigurations(inst, hard).count == 0,
          "the same record read truthfully admits nothing");
    check(sweep > 0, "yet its evidence under the channel is positive");

    // On a record some board does satisfy, eps -> 0 has to return the count.
    std::vector<int> feasible(16, -1);
    for (int c : {0, 1, 2, 8, 9}) feasible[static_cast<std::size_t>(c)] = 1;
    for (int c : {5, 7}) feasible[static_cast<std::size_t>(c)] = 0;

    std::vector<mayflower::CellConstraint> hard2(16, mayflower::CellConstraint::Free);
    for (int c : {0, 1, 2, 8, 9}) hard2[static_cast<std::size_t>(c)] =
        mayflower::CellConstraint::MustBeOccupied;
    for (int c : {5, 7}) hard2[static_cast<std::size_t>(c)] =
        mayflower::CellConstraint::MustBeEmpty;
    const std::uint64_t hardCount = mayflower::countConfigurations(inst, hard2).count;

    const mayflower::Weights sharp = mayflower::Weights::noisyChannel(inst, feasible, 1e-9);
    // Each of the seven answers contributes very nearly (1-eps), so scale it out.
    const double scaled =
        mayflower::weightedCount(inst, sharp).total / std::pow(1.0 - 1e-9, 7);
    check(hardCount > 0, "the second record is satisfiable (" +
                             std::to_string(hardCount) + " boards)");
    check(close(scaled, static_cast<double>(hardCount), 1e-6),
          "eps -> 0 recovers that count exactly");
    std::printf("      eps=1e-9 gives %.10f against %llu\n", scaled,
                static_cast<unsigned long long>(hardCount));
}

// Under uniform weights the marginals must still sum to the ship-cell count.
void testMarginalsSum() {
    std::printf("[marginals under weights]\n");
    const mayflower::Instance inst(5, 5, {4, 3, 2});
    mayflower::Constraints free;
    free.cells.assign(25, mayflower::CellConstraint::Free);

    // The two routes share no code: one is a forward-backward pass, the other a
    // constrained recount per cell. Agreement is a real check.
    const auto flat = mayflower::weightedMarginals(inst, free, mayflower::Weights::uniform());
    const auto slow = mayflower::weightedMarginalsByRecount(inst, free,
                                                            mayflower::Weights::uniform());
    double worst = 0;
    for (std::size_t i = 0; i < flat.size(); ++i)
        worst = std::max(worst, std::abs(flat[i] - slow[i]));
    check(worst < 1e-12, "forward-backward agrees with the recount, uniform weights");
    double sum = 0;
    for (double v : flat) sum += v;
    check(close(sum, inst.shipCells(), 1e-12),
          "uniform weights: marginals sum to the ship-cell count");

    const mayflower::Weights w = drawWeights(inst, 2024);
    const auto tilted = mayflower::weightedMarginals(inst, free, w);
    const auto tiltedSlow = mayflower::weightedMarginalsByRecount(inst, free, w);
    worst = 0;
    for (std::size_t i = 0; i < tilted.size(); ++i)
        worst = std::max(worst, std::abs(tilted[i] - tiltedSlow[i]));
    check(worst < 1e-12, "and under tilted weights too");
    double tiltedSum = 0;
    for (double v : tilted) tiltedSum += v;
    check(close(tiltedSum, inst.shipCells(), 1e-12),
          "and still do once the weights are tilted");

    bool moved = false;
    for (std::size_t i = 0; i < flat.size(); ++i)
        if (std::abs(flat[i] - tilted[i]) > 1e-6) moved = true;
    check(moved, "tilting the weights actually moves the marginals");
}

// Scaling every cell weight by k multiplies each configuration's weight by
// k^cellCount, so the log has to move by exactly cellCount*log(k). Driving k far
// enough forces the rescaling path, which is otherwise never exercised.
void testRescaling() {
    std::printf("[rescaling]\n");
    const mayflower::Instance inst(5, 5, {4, 3, 2});
    const int n = inst.cellCount();

    const mayflower::Weights base = drawWeights(inst, 5150);
    const auto plain = mayflower::weightedCount(inst, base);
    check(!plain.rescaled, "moderate weights need no rescaling");

    for (double k : {1e-13, 1e13}) {
        mayflower::Weights scaled = base;
        for (double& v : scaled.occupied) v *= k;
        for (double& v : scaled.empty) v *= k;
        const auto r = mayflower::weightedCount(inst, scaled);

        const double expected = plain.logTotal + n * std::log(k);
        char label[160];
        std::snprintf(label, sizeof label, "k = %.0e: log moves by exactly n*log(k)", k);
        check(r.rescaled, std::string("k = ") + (k < 1 ? "1e-13" : "1e13") +
                              ": the rescaling path fired");
        check(close(r.logTotal, expected, 1e-13), label);
        if (!close(r.logTotal, expected, 1e-13))
            std::printf("      got %.17g, expected %.17g, relative %.3e\n", r.logTotal,
                        expected, std::abs(r.logTotal - expected) / std::abs(expected));
    }

    // Rescaling divides by a power of two, so it must not perturb a result that
    // is exactly representable. Unit weights scaled by 2^-40 per cell stay exact.
    mayflower::Weights halves;
    halves.occupied.assign(static_cast<std::size_t>(n), std::ldexp(1.0, -40));
    halves.empty.assign(static_cast<std::size_t>(n), std::ldexp(1.0, -40));
    const auto exact = mayflower::weightedCount(inst, halves);
    const double target =
        std::ldexp(static_cast<double>(mayflower::countConfigurations(inst).count), -40 * n);
    check(exact.total == target || close(exact.total, target, 1e-15),
          "powers of two in, powers of two out, with no drift");
}

void testRejectsBadInput() {
    std::printf("[input validation]\n");
    const mayflower::Instance inst(4, 4, {3, 2});
    bool threw = false;
    try {
        mayflower::Weights::noisyChannel(inst, std::vector<int>(16, -1), 0.0);
    } catch (const std::exception&) { threw = true; }
    check(threw, "eps = 0 is refused, since it is a hard constraint");

    threw = false;
    try {
        mayflower::Weights bad;
        bad.occupied.assign(3, 1.0);
        mayflower::weightedCount(inst, bad);
    } catch (const std::exception&) { threw = true; }
    check(threw, "a mis-sized weight vector is refused");
}

}  // namespace

int main() {
    std::printf("weighted counting\n=================\n");
    testUnweightedBridge();
    testAgainstEnumeration();
    testPlacementWeightsOnly();
    testNoisyChannel();
    testMarginalsSum();
    testRescaling();
    testRejectsBadInput();
    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
