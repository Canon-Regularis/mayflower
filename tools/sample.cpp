// sample: draw uniform boards for the standard instance by unranking, and check
// the draws against the exact marginals.
//
// This is the board generator. Sequential rejection placement is not uniform, so
// any statistic collected with it is unquotable.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

struct SplitMix {
    std::uint64_t s;
    explicit SplitMix(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Unbiased draw from [0, n) by rejection on the ragged tail.
    std::uint64_t below(std::uint64_t n) {
        const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % n) - 1;
        std::uint64_t r;
        do { r = next(); } while (r > limit);
        return r % n;
    }
};

}  // namespace

int main() {
    using namespace mayflower;
    namespace k = mayflower::constants;

    const Instance inst = standardInstance();
    const int W = inst.width, H = inst.height;

    std::printf("Uniform board generation by unranking, %s\n", inst.describe().c_str());
    std::printf("=========================================================\n\n");

    const auto t0 = std::chrono::steady_clock::now();
    const Sampler sampler(inst);
    const double buildSeconds = seconds(t0);
    const std::uint64_t total = sampler.total();

    std::printf("|Omega|          %llu   %s\n", static_cast<unsigned long long>(total),
                total == k::kOmega0 ? "(matches constants.hpp)" : "*** MISMATCH ***");
    std::printf("build time       %.3f s\n", buildSeconds);
    std::printf("stored entries   %llu  (about %.0f MB of backward counts)\n\n",
                static_cast<unsigned long long>(sampler.storedEntries()),
                static_cast<double>(sampler.storedEntries()) * 24.0 / (1024.0 * 1024.0));

    // Exact marginals to compare against.
    std::uint64_t exactTotal = 0;
    const std::vector<std::uint64_t> exact = occupancyMap(inst, exactTotal);

    const int samples = 200000;
    SplitMix rng(0x5EED1234u);
    std::vector<std::uint64_t> counted(static_cast<std::size_t>(inst.cellCount()), 0);
    int badFleet = 0, overlaps = 0;

    const auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < samples; ++i) {
        const auto ships = sampler.unrank(rng.below(total));
        if (static_cast<int>(ships.size()) != static_cast<int>(inst.fleet.size())) ++badFleet;

        std::vector<char> seen(static_cast<std::size_t>(inst.cellCount()), 0);
        int cells = 0;
        for (const auto& s : ships) {
            for (int t = 0; t < s.length; ++t) {
                const int cell = s.horizontal ? s.row * W + s.col + t : (s.row + t) * W + s.col;
                if (seen[static_cast<std::size_t>(cell)]) ++overlaps;
                seen[static_cast<std::size_t>(cell)] = 1;
                ++counted[static_cast<std::size_t>(cell)];
                ++cells;
            }
        }
        if (cells != inst.shipCells()) ++badFleet;
    }
    const double drawSeconds = seconds(t1);

    std::printf("drew %d boards in %.3f s  (%.1f us per board, %.0f boards/s)\n",
                samples, drawSeconds, drawSeconds * 1e6 / samples, samples / drawSeconds);
    std::printf("fleet violations %d, overlapping cells %d\n\n", badFleet, overlaps);

    // Empirical against exact, per cell.
    double maxDev = 0.0;
    double chi2 = 0.0;
    for (std::size_t i = 0; i < exact.size(); ++i) {
        const double p = static_cast<double>(exact[i]) / static_cast<double>(exactTotal);
        const double q = static_cast<double>(counted[i]) / samples;
        maxDev = std::max(maxDev, std::abs(p - q));
        const double expected = p * samples;
        if (expected > 0.0) {
            const double d = static_cast<double>(counted[i]) - expected;
            chi2 += d * d / (expected * (1.0 - p));   // per-cell binomial variance
        }
    }
    std::printf("largest per-cell deviation from the exact marginal  %.5f\n", maxDev);
    std::printf("sum of squared standardised residuals over 100 cells %.1f\n", chi2);
    std::printf("  (cells are dependent, so this is a magnitude check, not a calibrated test;\n"
                "   the exhaustive unrank bijection in tests/test_sampler.cpp is the proof)\n\n");

    std::printf("empirical occupancy, %d samples:\n      ", samples);
    for (int c = 0; c < W; ++c) std::printf("%8d", c);
    std::printf("\n");
    for (int r = 0; r < H; ++r) {
        std::printf("  r%d  ", r);
        for (int c = 0; c < W; ++c)
            std::printf("%8.4f", static_cast<double>(counted[static_cast<std::size_t>(r * W + c)]) / samples);
        std::printf("\n");
    }

    const bool ok = total == k::kOmega0 && badFleet == 0 && overlaps == 0 && maxDev < 0.005;
    std::printf("\n%s\n", ok ? "OK" : "*** CHECK FAILED ***");
    return ok ? 0 : 1;
}
