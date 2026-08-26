// V3 under repetition.
//
// The ladder test checks each thread count once per case, which proves the
// decomposition is right but says little about a race: a race that fires one
// time in fifty passes a single run comfortably. This runs the same sweep many
// times at many thread counts and demands the identical integer every time.
//
// The instance matters. Layers below kParallelFloor merge serially whatever the
// caller asks for, so a small board would exercise nothing. 8x8 {5,4,3,3,2}
// averages well above the floor, so the pool is genuinely running.
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/profile_dp_blocked.hpp"

namespace {

using namespace mayflower;

int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  %-58s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!detail.empty()) std::printf("      %s\n", detail.c_str());
    if (!ok) ++failures;
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

// One answer, however many threads produced it and however often.
void testRepeatedAgreement() {
    std::printf("[repetition, above the parallel floor]\n");
    const Instance inst(8, 8, {5, 4, 3, 3, 2});
    const std::uint64_t reference = countConfigurations(inst).count;

    std::set<std::uint64_t> seen;
    int sweeps = 0;
    for (int rep = 0; rep < 4; ++rep)
        for (int threads : {1, 2, 3, 5, 8, 10}) {
            seen.insert(countConfigurationsBlocked(inst, threads).count);
            ++sweeps;
        }

    char buf[128];
    std::snprintf(buf, sizeof buf, "%d sweeps, %zu distinct answers", sweeps, seen.size());
    check(seen.size() == 1 && *seen.begin() == reference,
          "every sweep returns the one integer V0 does", buf);
}

// Constrained layers are smaller and shaped differently, so they exercise the
// floor and the bucket balance rather than the steady state.
void testConstrainedAgreement() {
    std::printf("[repetition under constraints]\n");
    const Instance inst(8, 8, {5, 4, 3, 3, 2});
    Rng rng(0x51DE51DE);
    int agreed = 0, trials = 0, engaged = 0;

    for (int t = 0; t < 12; ++t) {
        std::vector<CellConstraint> cells(64, CellConstraint::Free);
        for (int i = 0; i < 64; ++i) {
            const int r = rng.below(8);
            if (r == 0) cells[static_cast<std::size_t>(i)] = CellConstraint::MustBeEmpty;
        }
        const CountResult v0 = countConfigurations(inst, cells);
        if (v0.count == 0) continue;
        if (v0.edges / 64 >= 8000) ++engaged;

        for (int threads : {1, 4, 9}) {
            ++trials;
            if (countConfigurationsBlocked(inst, cells, threads).count == v0.count) ++agreed;
            else
                std::printf("      trial %d, %d threads: %llu against V0 %llu\n", t, threads,
                            static_cast<unsigned long long>(
                                countConfigurationsBlocked(inst, cells, threads).count),
                            static_cast<unsigned long long>(v0.count));
        }
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "%d/%d constrained sweeps agree with V0", agreed, trials);
    check(agreed == trials && trials > 0, buf);
    std::snprintf(buf, sizeof buf, "%d of the records stayed above the parallel floor",
                  engaged);
    check(engaged > 0, buf);
}

// More threads than buckets, and more than the machine has. Neither should do
// anything except waste time.
void testDegenerateThreadCounts() {
    std::printf("[degenerate thread counts]\n");
    const Instance inst(7, 7, {5, 4, 3, 2});
    const std::uint64_t reference = countConfigurations(inst).count;
    bool ok = true;
    for (int threads : {0, -3, 1, 64, 200}) {
        const std::uint64_t got = countConfigurationsBlocked(inst, threads).count;
        if (got != reference) {
            ok = false;
            std::printf("      %d threads gave %llu, expected %llu\n", threads,
                        static_cast<unsigned long long>(got),
                        static_cast<unsigned long long>(reference));
        }
    }
    check(ok, "zero, negative and absurd thread counts all return the same answer");
}

}  // namespace

int main() {
    std::printf("V3 under repetition\n===================\n");
    testRepeatedAgreement();
    testConstrainedAgreement();
    testDegenerateThreadCounts();
    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
