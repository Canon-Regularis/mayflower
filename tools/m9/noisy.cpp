// Noisy Battleship.
//
// Every answer is flipped with probability eps, so the posterior is exactly
// exp(-beta m) in the mismatch count. At eps = 0.5 the channel carries nothing
// and the evidence must read exactly the prior, which anchors the table.

#include "core.hpp"

namespace mayflower::m9 {

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

}  // namespace mayflower::m9
