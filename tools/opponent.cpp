// The unlearnability audit.
//
// A weighted sweep can carry an opponent prior, so the engine can exploit a
// player who hugs the edges or favours one orientation. The question this tool
// settles is whether that is worth anything against a real opponent, where the
// prior is not given but has to be learned from the games already played.
//
// Everything here is exact. Every board is enumerated, so the prior is a vector
// of exact weights and the expected shots of a policy under that prior is a
// weighted sum over all boards rather than a sample mean. The only sampling is
// in the learning itself, which is the thing under audit.
//
// Three quantities per opponent:
//
//   uniform     what the engine scores assuming a uniform prior, which is what
//               it does today
//   oracle      what it scores handed the opponent's exact prior. The most any
//               amount of learning could ever be worth
//   learned     what it scores after fitting the prior from N observed boards
//
// The audit is the gap between the second and the third, read against N.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "mayflower/game.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/policy.hpp"
#include "mayflower/profile_dp.hpp"

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
};

// Every board, with enough structure to answer a shot exactly.
struct World {
    int cells = 0;
    int placementSlots = 0;
    std::vector<std::uint32_t> occupancy;
    std::vector<std::vector<std::int8_t>> owner;
    std::vector<std::vector<std::uint32_t>> shipMask;
    std::vector<std::vector<std::int8_t>> shipLength;
    std::vector<std::vector<int>> slots;      // placement slot index per ship
    std::vector<double> border;               // per board, summed over ships
    std::vector<std::vector<ShipPlacement>> placements;
};

// Normalised distance from a ship's origin to the nearest edge, 0 at the border
// and 1 at the centre. One feature, so the prior is a one-parameter family and
// the fit has a sufficient statistic.
double borderScore(const Instance& inst, const ShipPlacement& p) {
    const double half = (std::min(inst.width, inst.height) - 1) / 2.0;
    const int d = std::min(std::min(p.row, inst.height - 1 - p.row),
                           std::min(p.col, inst.width - 1 - p.col));
    return half > 0 ? d / half : 0.0;
}

World buildWorld(const Instance& inst) {
    const Sampler sampler(inst);
    const std::uint64_t total = sampler.total();

    World w;
    w.cells = inst.cellCount();
    w.placementSlots = placementSlots(inst);
    for (std::uint64_t r = 0; r < total; ++r) {
        const auto ps = sampler.unrank(r);
        std::uint32_t occ = 0;
        std::vector<std::int8_t> own(static_cast<std::size_t>(w.cells), -1);
        std::vector<std::uint32_t> masks;
        std::vector<std::int8_t> lens;
        std::vector<int> slotIds;
        double border = 0;

        const std::vector<int> lengths = inst.distinctLengths();
        for (std::size_t i = 0; i < ps.size(); ++i) {
            const ShipPlacement& p = ps[i];
            std::uint32_t m = 0;
            for (int k = 0; k < p.length; ++k) {
                const int cell = p.horizontal ? p.row * inst.width + p.col + k
                                              : (p.row + k) * inst.width + p.col;
                m |= std::uint32_t{1} << cell;
                own[static_cast<std::size_t>(cell)] = static_cast<std::int8_t>(i);
            }
            occ |= m;
            masks.push_back(m);
            lens.push_back(static_cast<std::int8_t>(p.length));
            const std::size_t li = static_cast<std::size_t>(
                std::find(lengths.begin(), lengths.end(), p.length) - lengths.begin());
            slotIds.push_back(static_cast<int>(
                placementIndex(inst, p.row, p.col, static_cast<int>(li), p.horizontal)));
            border += borderScore(inst, p);
        }
        w.occupancy.push_back(occ);
        w.owner.push_back(std::move(own));
        w.shipMask.push_back(std::move(masks));
        w.shipLength.push_back(std::move(lens));
        w.slots.push_back(std::move(slotIds));
        w.border.push_back(border);
        w.placements.push_back(ps);
    }
    return w;
}

int outcomeOf(const World& w, std::size_t b, int cell, std::uint32_t shot) {
    if ((w.occupancy[b] & (std::uint32_t{1} << cell)) == 0) return 0;      // miss
    const int s = w.owner[b][static_cast<std::size_t>(cell)];
    const std::uint32_t others =
        w.shipMask[b][static_cast<std::size_t>(s)] & ~(std::uint32_t{1} << cell);
    if ((others & ~shot) != 0) return 1;                                    // plain hit
    return 2 + w.shipLength[b][static_cast<std::size_t>(s)];                // sunk
}

// A policy that believes some prior over boards and shoots the cell with the
// highest posterior occupancy under it. Survivors are carried across the game
// rather than recomputed, so a whole tournament stays affordable.
class BeliefPolicy : public Policy {
public:
    BeliefPolicy(const World& w, std::vector<double> prior)
        : w_(w), prior_(std::move(prior)) {}

    [[nodiscard]] const char* name() const override { return "belief"; }

    [[nodiscard]] int chooseShot(const Instance& inst, const History& h) override {
        const auto& seq = h.sequence();
        if (seq.empty()) {
            survivors_.resize(w_.occupancy.size());
            for (std::size_t i = 0; i < survivors_.size(); ++i) survivors_[i] = i;
            shot_ = 0;
            seen_ = 0;
        }
        // Fold in every shot the history has that we have not yet applied.
        while (seen_ < seq.size()) {
            const int cell = seq[seen_];
            const Outcome o = h.outcome(cell);
            const int want = o == Outcome::Miss ? 0
                             : o == Outcome::Hit ? 1
                                                 : 2 + h.sunkLength(cell);
            std::vector<std::size_t> kept;
            kept.reserve(survivors_.size());
            for (std::size_t b : survivors_)
                if (outcomeOf(w_, b, cell, shot_) == want) kept.push_back(b);
            survivors_.swap(kept);
            shot_ |= std::uint32_t{1} << cell;
            ++seen_;
        }

        std::vector<double> mass(static_cast<std::size_t>(inst.cellCount()), 0.0);
        for (std::size_t b : survivors_) {
            const double p = prior_[b];
            std::uint32_t occ = w_.occupancy[b] & ~shot_;
            while (occ) {
                const int cell = __builtin_ctz(occ);
                occ &= occ - 1;
                mass[static_cast<std::size_t>(cell)] += p;
            }
        }

        int best = -1;
        double bestMass = -1.0;
        for (int c = 0; c < inst.cellCount(); ++c) {
            if ((shot_ >> c) & 1u) continue;
            if (mass[static_cast<std::size_t>(c)] > bestMass + 1e-18) {
                bestMass = mass[static_cast<std::size_t>(c)];
                best = c;
            }
        }
        if (best >= 0) return best;
        for (int c = 0; c < inst.cellCount(); ++c) if (!h.shot(c)) return c;
        return -1;
    }

private:
    const World& w_;
    std::vector<double> prior_;
    std::vector<std::size_t> survivors_;
    std::uint32_t shot_ = 0;
    std::size_t seen_ = 0;
};

std::vector<double> normalise(std::vector<double> v) {
    double s = 0;
    for (double x : v) s += x;
    if (s > 0) for (double& x : v) x /= s;
    return v;
}

// The opponent: weight proportional to exp(-theta * total border score), so a
// positive theta pushes ships toward the edges.
std::vector<double> edgePrior(const World& w, double theta) {
    std::vector<double> p(w.occupancy.size());
    for (std::size_t b = 0; b < p.size(); ++b) p[b] = std::exp(-theta * w.border[b]);
    return normalise(std::move(p));
}

std::vector<double> uniformPrior(const World& w) {
    return normalise(std::vector<double>(w.occupancy.size(), 1.0));
}

// Exact expected shots: every board played once, weighted by how often the
// opponent actually produces it. No sampling anywhere in the evaluation.
double expectedShots(const Instance& inst, const World& w, const std::vector<double>& truth,
                     const std::vector<double>& believed) {
    BeliefPolicy policy(w, believed);
    double total = 0;
    for (std::size_t b = 0; b < w.occupancy.size(); ++b) {
        if (truth[b] <= 0) continue;
        total += truth[b] * playGame(inst, w.placements[b], policy, 1).shots;
    }
    return total;
}

double expectedBorder(const World& w, const std::vector<double>& p) {
    double t = 0;
    for (std::size_t b = 0; b < p.size(); ++b) t += p[b] * w.border[b];
    return t;
}

// Moment matching. The border score is the sufficient statistic of this
// one-parameter family, so matching its mean recovers theta and bisection is
// enough because the mean is monotone in theta.
double fitTheta(const World& w, double targetBorder) {
    double lo = -8, hi = 8;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (expectedBorder(w, edgePrior(w, mid)) > targetBorder) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

std::size_t sampleBoard(const std::vector<double>& cdf, Rng& rng) {
    const double u = rng.unit();
    return static_cast<std::size_t>(
        std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin());
}

std::vector<double> makeCdf(const std::vector<double>& p) {
    std::vector<double> c(p.size());
    double run = 0;
    for (std::size_t i = 0; i < p.size(); ++i) { run += p[i]; c[i] = run; }
    if (!c.empty()) c.back() = 1.0;
    return c;
}

void audit(const Instance& inst, const std::vector<double>& thetas, int replicates) {
    const World w = buildWorld(inst);
    const std::size_t N = w.occupancy.size();
    std::printf("\n%s, %llu boards, %d placement slots\n", inst.describe().c_str(),
                static_cast<unsigned long long>(N), w.placementSlots);

    const std::vector<double> flat = uniformPrior(w);
    for (double theta : thetas) {
        const std::vector<double> truth = edgePrior(w, theta);
        const std::vector<double> cdf = makeCdf(truth);

        const double eUniform = expectedShots(inst, w, truth, flat);
        const double eOracle = expectedShots(inst, w, truth, truth);
        const double oracleGain = eUniform - eOracle;

        std::printf("\n  theta %.1f   mean border %.4f (uniform %.4f)\n", theta,
                    expectedBorder(w, truth), expectedBorder(w, flat));
        std::printf("    assuming uniform      %8.4f shots\n", eUniform);
        std::printf("    handed the true prior %8.4f shots   oracle gain %+.4f\n", eOracle,
                    -oracleGain);

        std::printf("    %8s %14s %14s %10s\n", "games N", "fitted theta", "shots",
                    "of oracle");
        for (int games : {5, 10, 25, 50, 100, 250, 1000}) {
            double sumShots = 0, sumTheta = 0;
            for (int rep = 0; rep < replicates; ++rep) {
                Rng rng(0xA11CEull + static_cast<std::uint64_t>(games) * 1000 +
                        static_cast<std::uint64_t>(rep));
                double borderSum = 0;
                for (int g = 0; g < games; ++g)
                    borderSum += w.border[sampleBoard(cdf, rng)];
                const double thetaHat = fitTheta(w, borderSum / games);
                sumTheta += thetaHat;
                sumShots += expectedShots(inst, w, truth, edgePrior(w, thetaHat));
            }
            const double shots = sumShots / replicates;
            const double captured = oracleGain > 1e-9 ? (eUniform - shots) / oracleGain : 0.0;
            std::printf("    %8d %14.3f %14.4f %9.0f%%\n", games, sumTheta / replicates,
                        shots, 100.0 * captured);
            std::fflush(stdout);
        }
    }
}

// The other half of the audit: how many games it takes to learn the prior when
// it is not assumed to have one parameter. Counting how often each placement
// appears is the honest nonparametric estimator, and there are hundreds of them.
void nonparametric(const Instance& inst) {
    const World w = buildWorld(inst);
    const double theta = 2.0;
    const std::vector<double> truth = edgePrior(w, theta);
    const std::vector<double> cdf = makeCdf(truth);
    const std::vector<double> flat = uniformPrior(w);

    const double eUniform = expectedShots(inst, w, truth, flat);
    const double eOracle = expectedShots(inst, w, truth, truth);
    const double oracleGain = eUniform - eOracle;

    std::printf("\n  Fitting placement frequencies instead, theta %.1f, %d slots to estimate\n",
                theta, w.placementSlots);
    std::printf("    %8s %14s %10s %12s\n", "games N", "shots", "of oracle", "slots seen");
    for (int games : {25, 100, 400, 1600, 6400}) {
        double sumShots = 0, sumSeen = 0;
        const int replicates = 3;
        for (int rep = 0; rep < replicates; ++rep) {
            Rng rng(0xBEE5ull + static_cast<std::uint64_t>(games) * 977 +
                    static_cast<std::uint64_t>(rep));
            // One pseudocount per slot, so an unseen placement stays possible.
            std::vector<double> count(static_cast<std::size_t>(w.placementSlots), 1.0);
            std::vector<char> seen(static_cast<std::size_t>(w.placementSlots), 0);
            for (int g = 0; g < games; ++g) {
                const std::size_t b = sampleBoard(cdf, rng);
                for (int slot : w.slots[b]) {
                    count[static_cast<std::size_t>(slot)] += 1.0;
                    seen[static_cast<std::size_t>(slot)] = 1;
                }
            }
            std::vector<double> q(w.occupancy.size(), 1.0);
            for (std::size_t b = 0; b < q.size(); ++b)
                for (int slot : w.slots[b]) q[b] *= count[static_cast<std::size_t>(slot)];
            sumShots += expectedShots(inst, w, truth, normalise(std::move(q)));
            int hit = 0;
            for (char c : seen) hit += c;
            sumSeen += hit;
        }
        const double shots = sumShots / replicates;
        const double captured = oracleGain > 1e-9 ? (eUniform - shots) / oracleGain : 0.0;
        std::printf("    %8d %14.4f %9.0f%% %11.0f\n", games, shots, 100.0 * captured,
                    sumSeen / replicates);
        std::fflush(stdout);
    }
    std::printf("\n    A reading above 100%% is not an error. The oracle here is the policy\n");
    std::printf("    that believes the TRUE prior, which is not the optimal policy: the shot\n");
    std::printf("    rule is greedy, so a belief slightly off the truth can score marginally\n");
    std::printf("    better. The column measures progress toward a reference, not toward a\n");
    std::printf("    ceiling.\n");
    std::printf("\n    Of %d slots the opponent ever uses only about the number in the last\n",
                w.placementSlots);
    std::printf("    column. The rest are unidentifiable from any number of games, which is\n");
    std::printf("    harmless: a placement the opponent never makes cannot be exploited.\n");
}

// Opponent modelling is a bet, and a bet has a losing side. Believing a bias the
// opponent does not have costs something, and that cost decides whether the
// gain above is worth taking.
void mismatch(const Instance& inst) {
    const World w = buildWorld(inst);
    const std::vector<double> thetas{0.0, 1.0, 2.0, 3.0};

    std::printf("\n  %s: rows are the opponent, columns what the engine believes\n",
                inst.describe().c_str());
    std::printf("    %10s", "actual");
    for (double b : thetas) std::printf(" %11.1f", b);
    std::printf(" %11s\n", "regret");

    for (double a : thetas) {
        const std::vector<double> truth = edgePrior(w, a);
        std::printf("    %10.1f", a);
        double bestScore = 1e9, believedFlat = 0;
        for (double b : thetas) {
            const double e = expectedShots(inst, w, truth, edgePrior(w, b));
            if (b == 0.0) believedFlat = e;
            bestScore = std::min(bestScore, e);
            std::printf(" %11.4f", e);
        }
        std::printf(" %11.4f\n", believedFlat - bestScore);
        std::fflush(stdout);
    }

    // The worst a fixed belief can do, over opponents.
    std::printf("\n    %10s %14s %14s\n", "believes", "worst case", "regret vs flat");
    double flatWorst = 0;
    std::vector<double> worst(thetas.size(), 0.0);
    for (std::size_t j = 0; j < thetas.size(); ++j) {
        for (double a : thetas)
            worst[j] = std::max(worst[j],
                                expectedShots(inst, w, edgePrior(w, a), edgePrior(w, thetas[j])));
        if (thetas[j] == 0.0) flatWorst = worst[j];
    }
    for (std::size_t j = 0; j < thetas.size(); ++j)
        std::printf("    %10.1f %14.4f %+14.4f\n", thetas[j], worst[j],
                    worst[j] - flatWorst);
    std::fflush(stdout);
}

// The invariants this audit rests on, cheap enough to run in CI.
int selfTest() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string& what) {
        std::printf("  %-58s %s\n", what.c_str(), ok ? "ok" : "FAILED");
        if (!ok) ++failures;
    };
    std::printf("opponent-model self-test\n------------------------\n");

    const Instance inst(4, 4, {3, 2});
    const World w = buildWorld(inst);
    const std::vector<double> flat = uniformPrior(w);

    double sum = 0;
    for (double p : flat) sum += p;
    check(std::abs(sum - 1.0) < 1e-12, "the uniform prior sums to one");

    check(std::abs(expectedBorder(w, edgePrior(w, 0.0)) - expectedBorder(w, flat)) < 1e-12,
          "theta = 0 reproduces the uniform prior");

    // A bias the engine knows about can only help against that opponent.
    for (double theta : {1.0, 2.0, 3.0}) {
        const std::vector<double> truth = edgePrior(w, theta);
        const double believing = expectedShots(inst, w, truth, truth);
        const double flatBelief = expectedShots(inst, w, truth, flat);
        check(believing <= flatBelief + 1e-9,
              "theta " + std::to_string(static_cast<int>(theta)) +
                  ": knowing the prior does not hurt");
        check(expectedBorder(w, truth) < expectedBorder(w, flat),
              "theta " + std::to_string(static_cast<int>(theta)) +
                  ": the bias moves ships toward the edge");
    }

    // Moment matching has to invert the family it came from.
    for (double theta : {-1.0, 0.5, 2.5}) {
        const double recovered = fitTheta(w, expectedBorder(w, edgePrior(w, theta)));
        check(std::abs(recovered - theta) < 1e-6,
              "moment matching recovers theta = " + std::to_string(theta).substr(0, 4));
    }

    // The mild permanent assumption really does dominate in the worst case.
    double worstFlat = 0, worstMild = 0;
    for (double a : {0.0, 1.0, 2.0, 3.0}) {
        const std::vector<double> truth = edgePrior(w, a);
        worstFlat = std::max(worstFlat, expectedShots(inst, w, truth, flat));
        worstMild = std::max(worstMild, expectedShots(inst, w, truth, edgePrior(w, 1.0)));
    }
    check(worstMild < worstFlat,
          "believing a mild bias has the better worst case");
    std::printf("      worst case: flat %.4f, mild %.4f\n", worstFlat, worstMild);

    std::printf("\n%s\n", failures ? "SELF-TEST FAILED" : "all invariants hold");
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const bool all = only.empty();

    // Before the banner, so the test output is only the test.
    if (only == "selftest") return selfTest();

    std::printf("Mayflower, the unlearnability audit\n");
    std::printf("===================================\n\n");
    std::printf("The engine can carry an opponent prior. Whether that helps against a real\n");
    std::printf("opponent depends on learning the prior from games already played, and this\n");
    std::printf("is what that costs. Evaluation is exact: every board is enumerated and\n");
    std::printf("weighted by how often the opponent produces it, so the only sampling is in\n");
    std::printf("the learning.\n");

    if (all || only == "fit") {
        std::printf("\n1. One parameter, and how fast it converges\n");
        std::printf("------------------------------------------\n");
        audit(Instance(4, 4, {3, 2}), {1.0, 3.0}, 12);
        audit(Instance(5, 5, {4, 3, 2}), {3.0}, 6);
    }
    if (all || only == "mismatch") {
        std::printf("\n3. What it costs to believe a bias that is not there\n");
        std::printf("---------------------------------------------------\n");
        mismatch(Instance(4, 4, {3, 2}));
        mismatch(Instance(5, 5, {4, 3, 2}));
    }
    if (all || only == "slots") {
        std::printf("\n2. The same prior, learned without assuming its shape\n");
        std::printf("----------------------------------------------------\n");
        nonparametric(Instance(4, 4, {3, 2}));
        nonparametric(Instance(5, 5, {4, 3, 2}));
    }
    return 0;
}
