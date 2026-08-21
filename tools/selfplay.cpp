// selfplay: measure shot counts for the cheap-tier policies on one seeded board
// pool, with paired comparisons.
//
// All policies play the same boards, so differences are paired and the variance
// of a difference is far smaller than the variance of either arm.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/game.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/policy.hpp"

namespace {

struct Summary {
    std::string name;
    double mean = 0, sd = 0, ciHalfWidth = 0;
    int median = 0, p95 = 0, worst = 0, best = 0;
    double secondsPerGame = 0;
    std::vector<int> shots;
};

double quantile(std::vector<int> v, double q) {
    std::sort(v.begin(), v.end());
    const std::size_t i = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

Summary summarise(const std::string& name, std::vector<int> shots, double seconds) {
    Summary s;
    s.name = name;
    s.shots = shots;
    const double n = static_cast<double>(shots.size());
    double sum = 0;
    for (int v : shots) sum += v;
    s.mean = sum / n;
    double ss = 0;
    for (int v : shots) ss += (v - s.mean) * (v - s.mean);
    s.sd = std::sqrt(ss / (n - 1));
    s.ciHalfWidth = 1.959964 * s.sd / std::sqrt(n);
    s.median = static_cast<int>(quantile(shots, 0.50));
    s.p95 = static_cast<int>(quantile(shots, 0.95));
    s.best = *std::min_element(shots.begin(), shots.end());
    s.worst = *std::max_element(shots.begin(), shots.end());
    s.secondsPerGame = seconds / n;
    return s;
}

double correlation(const Summary& a, const Summary& b) {
    const std::size_t n = a.shots.size();
    double ca = 0, cb = 0;
    for (std::size_t i = 0; i < n; ++i) { ca += a.shots[i]; cb += b.shots[i]; }
    ca /= static_cast<double>(n);
    cb /= static_cast<double>(n);
    double cov = 0, va = 0, vb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = a.shots[i] - ca, y = b.shots[i] - cb;
        cov += x * y; va += x * x; vb += y * y;
    }
    if (va <= 0 || vb <= 0) return 0.0;
    return cov / std::sqrt(va * vb);
}

void pairedComparison(const Summary& a, const Summary& b) {
    const std::size_t n = a.shots.size();
    double dsum = 0;
    for (std::size_t i = 0; i < n; ++i) dsum += a.shots[i] - b.shots[i];
    const double dmean = dsum / static_cast<double>(n);
    double dss = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = (a.shots[i] - b.shots[i]) - dmean;
        dss += d * d;
    }
    const double dsd = std::sqrt(dss / static_cast<double>(n - 1));
    const double half = 1.959964 * dsd / std::sqrt(static_cast<double>(n));

    // Correlation across the shared board pool, which is what the pairing buys.
    double ca = 0, cb = 0;
    for (std::size_t i = 0; i < n; ++i) { ca += a.shots[i]; cb += b.shots[i]; }
    ca /= static_cast<double>(n);
    cb /= static_cast<double>(n);
    double cov = 0, va = 0, vb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = a.shots[i] - ca, y = b.shots[i] - cb;
        cov += x * y; va += x * x; vb += y * y;
    }
    const double rho = cov / std::sqrt(va * vb);
    const double independentHalf =
        1.959964 * std::sqrt(a.sd * a.sd + b.sd * b.sd) / std::sqrt(static_cast<double>(n));

    std::printf("  %-20s - %-20s  %+7.3f  [%+7.3f, %+7.3f]   rho %.3f   CRN saves %.1fx\n",
                a.name.c_str(), b.name.c_str(), dmean, dmean - half, dmean + half, rho,
                (independentHalf / half) * (independentHalf / half));
}

}  // namespace

int main(int argc, char** argv) {
    using namespace mayflower;
    namespace k = mayflower::constants;

    const int games = argc > 1 ? std::atoi(argv[1]) : 20000;
    const std::uint64_t poolKey = 0xA1B2C3D4u;

    const Instance inst = standardInstance();
    std::printf("Self-play on a seeded uniform board pool, %s\n", inst.describe().c_str());
    std::printf("======================================================\n\n");

    const auto tBank = std::chrono::steady_clock::now();
    const BoardBank bank(inst, poolKey);
    std::printf("board pool   %llu configurations, key 0x%llX, %d games\n",
                static_cast<unsigned long long>(bank.total()),
                static_cast<unsigned long long>(poolKey), games);
    std::printf("bank build   %.2f s\n\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - tBank).count());

    // Boards are drawn once and reused, so every policy sees the same pool.
    std::vector<std::vector<ShipPlacement>> boards;
    boards.reserve(static_cast<std::size_t>(games));
    for (int i = 0; i < games; ++i) boards.push_back(bank.board(static_cast<std::uint64_t>(i)));

    // Policy seeds come from a stream keyed separately from the board pool, so a
    // stochastic policy's randomness stays independent of which board it faces.
    // Sharing one counter would tie the two together and bias the estimate.
    std::vector<std::uint64_t> policySeeds(static_cast<std::size_t>(games));
    for (int i = 0; i < games; ++i) {
        std::uint64_t z = static_cast<std::uint64_t>(i) + 0xD1B54A32D192ED03ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        policySeeds[static_cast<std::size_t>(i)] = z ^ (z >> 31);
    }

    std::vector<std::unique_ptr<Policy>> policies;
    std::vector<std::string> names;
    policies.push_back(std::make_unique<RandomPolicy>());          names.push_back("random");
    policies.push_back(std::make_unique<ParityHuntTarget>());      names.push_back("parity-hunt-target");
    policies.push_back(std::make_unique<DensityPolicy>(10));       names.push_back("density(b=10)");
    policies.push_back(std::make_unique<DensityPolicy>(50));       names.push_back("density(b=50)");
    policies.push_back(std::make_unique<DensityPolicy>(200));      names.push_back("density(b=200)");

    std::vector<Summary> results;
    for (auto& policy : policies) {
        std::vector<int> shots;
        shots.reserve(static_cast<std::size_t>(games));
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < games; ++i)
            shots.push_back(playGame(inst, boards[static_cast<std::size_t>(i)], *policy,
                                     policySeeds[static_cast<std::size_t>(i)])
                                .shots);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        results.push_back(summarise(names[results.size()], std::move(shots), dt));
    }

    std::printf("%-20s %8s %7s %18s %7s %6s %6s %6s %10s\n", "policy", "mean", "sd",
                "95% CI on mean", "median", "p95", "best", "worst", "us/game");
    for (const Summary& s : results) {
        std::printf("%-20s %8.3f %7.3f  [%7.3f, %7.3f] %7d %6d %6d %6d %10.1f\n", s.name.c_str(),
                    s.mean, s.sd, s.mean - s.ciHalfWidth, s.mean + s.ciHalfWidth, s.median, s.p95,
                    s.best, s.worst, s.secondsPerGame * 1e6);
    }

    // Harness self-test. Shooting uniformly at random, the game ends on the last
    // of the 17 ship cells, so E[T] = k(N+1)/(k+1).
    const double expectedRandom = static_cast<double>(k::kShipCells) * (k::kCellCount + 1) /
                                  (k::kShipCells + 1);
    const Summary& rnd = results[0];
    const bool selfTestOk = std::abs(rnd.mean - expectedRandom) < rnd.ciHalfWidth * 1.5;
    std::printf("\nharness self-test: random shooter measured %.4f, theory k(N+1)/(k+1) = %.4f  %s\n",
                rnd.mean, expectedRandom, selfTestOk ? "OK" : "*** OFF ***");

    std::printf("\ncorrelation across the shared board pool:\n%-22s", "");
    for (const Summary& s : results) std::printf(" %10.10s", s.name.c_str());
    std::printf("\n");
    for (std::size_t i = 0; i < results.size(); ++i) {
        std::printf("%-22.22s", results[i].name.c_str());
        for (std::size_t j = 0; j < results.size(); ++j)
            std::printf(" %10.3f", correlation(results[i], results[j]));
        std::printf("\n");
    }
    std::printf("\nCommon random numbers pay off only where two policies share board-driven\n"
                "variance. Within the density family the correlation is high and the paired\n"
                "interval shrinks accordingly; against a stochastic hunt policy, whose variance\n"
                "comes mostly from its own draws, the pairing buys nothing. Sample sizes have to\n"
                "be derived per comparison from the measured correlation.\n");

    std::printf("\npaired differences (negative favours the first policy):\n");
    for (std::size_t i = 0; i + 1 < results.size(); ++i)
        for (std::size_t j = i + 1; j < results.size(); ++j)
            pairedComparison(results[i], results[j]);

    std::printf("\nbounds for reference: coverage %d shots, entropy %.2f shots\n",
                k::kCoverageBound, k::kEntropyBound);
    const Summary& bestPolicy = results.back();
    std::printf("gap from the coverage bound to %s: %.3f shots\n", bestPolicy.name.c_str(),
                bestPolicy.mean - k::kCoverageBound);

    return selfTestOk ? 0 : 1;
}
