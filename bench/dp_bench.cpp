// dp_bench: the optimisation ladder, measured under the protocol.
//
// Protocol, in order:
//   1. pin to one logical processor of a stated efficiency class;
//   2. warm up, discarding the first runs;
//   3. interleave the rungs ABBA so drift in one direction cancels the other;
//   4. report medians with the full spread, and an A/A control that measures the
//      noise floor by comparing a rung against itself;
//   5. refuse to claim a speedup smaller than the measured noise floor.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/platform.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct Sample {
    std::vector<double> times;
    std::uint64_t count = 0;
    std::uint64_t edges = 0;
};

void report(const char* name, const Sample& s) {
    auto t = s.times;
    std::sort(t.begin(), t.end());
    std::printf("  %-10s median %7.3f s   min %7.3f   max %7.3f   spread %.2fx   %6.1f ns/edge\n",
                name, median(t), t.front(), t.back(), t.back() / t.front(),
                median(t) * 1e9 / static_cast<double>(s.edges));
}

}  // namespace

int main(int argc, char** argv) {
    using namespace mayflower;

    const int reps = argc > 1 ? std::atoi(argv[1]) : 7;
    const bool slow = argc > 2 && std::string(argv[2]) == "--e-core";

    std::printf("DP optimisation ladder\n======================\n\n");
    std::printf("%s\n\n", platform::describeTopology().c_str());

    const bool pinned = slow ? platform::pinToSlowestCore() : platform::pinToFastestCore();
    std::printf("pinned to a %s core: %s\n", slow ? "slowest-class" : "fastest-class",
                pinned ? "yes" : "NO, results are not comparable");
    std::printf("reps %d, ABBA interleaved\n\n", reps);

    const Instance inst = standardInstance();

    // Warm up. The first runs pay for page faults and frequency ramp.
    for (int i = 0; i < 2; ++i) {
        (void)countConfigurationsFast(inst);
    }

    Sample v0, v1, control;
    for (int i = 0; i < reps; ++i) {
        // A B B A, then an extra A for the A/A control.
        {
            const auto t = Clock::now();
            const auto r = countConfigurations(inst);
            v0.times.push_back(std::chrono::duration<double>(Clock::now() - t).count());
            v0.count = r.count; v0.edges = r.edges;
        }
        {
            const auto t = Clock::now();
            const auto r = countConfigurationsFast(inst);
            v1.times.push_back(std::chrono::duration<double>(Clock::now() - t).count());
            v1.count = r.count; v1.edges = r.edges;
        }
        {
            const auto t = Clock::now();
            const auto r = countConfigurationsFast(inst);
            v1.times.push_back(std::chrono::duration<double>(Clock::now() - t).count());
            (void)r;
        }
        {
            const auto t = Clock::now();
            const auto r = countConfigurations(inst);
            v0.times.push_back(std::chrono::duration<double>(Clock::now() - t).count());
            (void)r;
        }
        {
            const auto t = Clock::now();
            const auto r = countConfigurationsFast(inst);
            control.times.push_back(std::chrono::duration<double>(Clock::now() - t).count());
            control.count = r.count; control.edges = r.edges;
        }
    }

    std::printf("V0  baseline map, 12-byte key struct, vector<bool> occupancy\n");
    report("V0", v0);
    std::printf("V1  packed uint64 key, epoch tagging, pre-sized, batched prefetch\n");
    report("V1", v1);
    std::printf("A/A control (V1 against itself, same binary, same core)\n");
    report("control", control);

    // The computation is deterministic, so every deviation above the fastest
    // observed run is interference added by the rest of the machine. The minimum
    // is therefore the best estimator of the true cost, and the ratio of minima
    // is the headline. It is biased low as an estimate of typical runtime, which
    // is why the median and the full spread are printed alongside it.
    auto v0t = v0.times, v1t = v1.times, c = control.times;
    std::sort(v0t.begin(), v0t.end());
    std::sort(v1t.begin(), v1t.end());
    std::sort(c.begin(), c.end());
    const double m0 = median(v0.times), m1 = median(v1.times);
    const double speedup = v0t.front() / v1t.front();
    const double medianSpeedup = m0 / m1;
    const double noiseFloor = c[c.size() / 2] / c.front();

    std::printf("\ncounts  V0 %llu   V1 %llu   %s\n", static_cast<unsigned long long>(v0.count),
                static_cast<unsigned long long>(v1.count),
                v0.count == v1.count ? "identical" : "*** MISMATCH ***");
    std::printf("edges   V0 %llu   V1 %llu   %s\n", static_cast<unsigned long long>(v0.edges),
                static_cast<unsigned long long>(v1.edges),
                v0.edges == v1.edges ? "identical" : "*** MISMATCH ***");
    std::printf("\nspeedup     %.2fx  (ratio of minima, the estimator of true cost)\n", speedup);
    std::printf("            %.2fx  (ratio of medians, includes interference)\n", medianSpeedup);
    std::printf("noise floor %.2fx  (A/A control, median over minimum)\n", noiseFloor);
    if (speedup > noiseFloor)
        std::printf("The effect exceeds the noise floor, so the speedup is reportable.\n");
    else
        std::printf("The effect is within the noise floor. NOT reportable; raise reps or\n"
                    "reduce interference before claiming anything.\n");

    return v0.count == v1.count ? 0 : 1;
}
