// The measurement platform, checked against the operating system rather than
// against its own return codes.
//
// Every figure in docs/BENCHMARKS.md rests on pinning working. The part is
// hybrid, 2 SMT P-cores plus 8 E-cores, and the docs record a 1.7x spread from
// core class alone, which is the size of the effects the ladder reports. A pin
// that silently failed would turn the whole ladder into noise and nothing would
// say so, because SetThreadAffinityMask reporting success is not the same as
// the thread having moved.
//
// So the check here is behavioural: pin, then ask the OS which processor the
// thread is actually running on, repeatedly, while doing work that gives the
// scheduler every chance to migrate it.

#include "mayflower/platform.hpp"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Everything below is Windows-only, the helpers included. Left outside the
// guard, `check` is defined and never called on the other platforms, which
// -Wunused-function reports and -Werror turns into a failed build on all three
// Linux legs.
#ifdef _WIN32

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    ++gChecks;
    std::printf("  %-62s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!detail.empty()) std::printf("      %s\n", detail.c_str());
    if (!ok) ++gFailures;
}

using namespace mayflower;

// Work the optimiser cannot delete, so the sampling loop occupies real time and
// the scheduler has a chance to migrate a thread that is not actually pinned.
volatile std::uint64_t gSink = 0;

std::uint64_t churn(int rounds) {
    std::uint64_t x = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < rounds; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    gSink = gSink + x;
    return x;
}

std::string hex(DWORD_PTR v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

DWORD_PTR processMask() {
    DWORD_PTR proc = 0, sys = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys)) return 0;
    return proc;
}

// The thread's current affinity, read by setting it to a known value and taking
// the previous mask the call returns, then putting it back.
DWORD_PTR currentThreadMask() {
    const DWORD_PTR probe = processMask();
    const DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), probe);
    if (prev == 0) return 0;
    SetThreadAffinityMask(GetCurrentThread(), prev);
    return prev;
}

void testTopologyMatchesTheSystem() {
    std::printf("[topology]\n");
    const auto cores = platform::enumerateCores();
    check(!cores.empty(), "the topology can be read at all");
    if (cores.empty()) return;

    const DWORD_PTR proc = processMask();
    int owned = 0;
    for (int b = 0; b < 64; ++b) if ((proc >> b) & 1u) ++owned;
    check(static_cast<int>(cores.size()) == owned,
          "the core count matches the process affinity mask",
          std::to_string(cores.size()) + " enumerated against " + std::to_string(owned) +
              " in mask " + hex(proc));

    std::vector<int> seen;
    for (const auto& c : cores) seen.push_back(c.index);
    std::sort(seen.begin(), seen.end());
    check(std::adjacent_find(seen.begin(), seen.end()) == seen.end(),
          "no logical index is enumerated twice");

    bool inMask = true;
    for (const auto& c : cores)
        if (c.index < 0 || c.index >= 64 || !((proc >> c.index) & 1u)) inMask = false;
    check(inMask, "every enumerated core is one the process may use");

    std::printf("%s\n", platform::describeTopology().c_str());
}

// The check that matters. A pin that reports success but leaves the thread free
// to migrate is the failure mode that would quietly invalidate the ladder.
void testPinningActuallyBinds() {
    std::printf("\n[pinning binds the thread]\n");
    const auto cores = platform::enumerateCores();
    if (cores.empty()) { check(false, "cores available to pin to"); return; }

    int strayed = 0;
    int pinFailures = 0;
    std::string firstStray;
    for (const auto& c : cores) {
        if (!platform::pinToCore(c.index)) { ++pinFailures; continue; }
        // Sampled across real work, not once immediately after the call. The
        // window per core spans many scheduler quanta on purpose: a pin that
        // takes and then drifts is as bad as one that never took.
        for (int s = 0; s < 64; ++s) {
            churn(400000);
            const DWORD on = GetCurrentProcessorNumber();
            if (static_cast<int>(on) != c.index) {
                ++strayed;
                if (firstStray.empty())
                    firstStray = "asked for " + std::to_string(c.index) + ", ran on " +
                                 std::to_string(on);
            }
        }
    }
    check(pinFailures == 0, "every core in the topology accepts a pin",
          std::to_string(pinFailures) + " refused");
    check(strayed == 0, "a pinned thread never runs on another processor",
          strayed ? firstStray : std::string("64 samples per core across ") +
                                     std::to_string(cores.size()) + " cores");
    platform::unpin();
}

void testUnpinRestoresEveryProcessor() {
    std::printf("\n[unpin]\n");
    const DWORD_PTR proc = processMask();
    const auto cores = platform::enumerateCores();
    if (cores.empty() || proc == 0) { check(false, "topology available"); return; }

    check(platform::pinToCore(cores.front().index), "pinned before unpinning");
    const DWORD_PTR pinned = currentThreadMask();
    check(pinned != proc, "the pin narrowed the affinity",
          hex(pinned) + " against process " + hex(proc));

    check(platform::unpin(), "unpin reports success");
    const DWORD_PTR after = currentThreadMask();
    check(after == proc, "and the thread owns every processor again",
          hex(after) + " against " + hex(proc));
}

void testInvalidPinsAreRefused() {
    std::printf("\n[bad input]\n");
    check(!platform::pinToCore(-1), "a negative index is refused");
    check(!platform::pinToCore(64), "an index past the mask width is refused");
    check(!platform::pinToCore(1000), "an absurd index is refused");

    // A processor the process does not own must be refused rather than silently
    // leaving the thread where it was.
    const DWORD_PTR proc = processMask();
    int absent = -1;
    for (int b = 0; b < 64; ++b) if (!((proc >> b) & 1u)) { absent = b; break; }
    if (absent >= 0)
        check(!platform::pinToCore(absent),
              "a processor outside the process mask is refused",
              "tried " + std::to_string(absent));
    else
        std::printf("  (the process owns all 64 slots, nothing outside to try)\n");
    platform::unpin();
}

void testClassSelection() {
    std::printf("\n[class selection]\n");
    const auto cores = platform::enumerateCores();
    if (cores.empty()) { check(false, "topology available"); return; }

    int fastest = cores.front().efficiencyClass, slowest = cores.front().efficiencyClass;
    for (const auto& c : cores) {
        fastest = std::max(fastest, c.efficiencyClass);
        slowest = std::min(slowest, c.efficiencyClass);
    }

    const auto classOf = [&](int index) {
        for (const auto& c : cores) if (c.index == index) return c.efficiencyClass;
        return -1;
    };

    check(platform::pinToFastestCore(), "a fastest-class core accepts a pin");
    churn(20000);
    const int fastOn = static_cast<int>(GetCurrentProcessorNumber());
    check(classOf(fastOn) == fastest, "and it belongs to the fastest class",
          "processor " + std::to_string(fastOn) + " is class " +
              std::to_string(classOf(fastOn)) + ", fastest is " + std::to_string(fastest));

    check(platform::pinToSlowestCore(), "a slowest-class core accepts a pin");
    churn(20000);
    const int slowOn = static_cast<int>(GetCurrentProcessorNumber());
    check(classOf(slowOn) == slowest, "and it belongs to the slowest class",
          "processor " + std::to_string(slowOn) + " is class " +
              std::to_string(classOf(slowOn)) + ", slowest is " + std::to_string(slowest));

    if (fastest != slowest)
        check(fastOn != slowOn, "the two classes are different processors",
              std::to_string(fastOn) + " and " + std::to_string(slowOn));
    else
        std::printf("  (one efficiency class on this machine, nothing to separate)\n");

    // Processor 0 takes device interrupts by default, so it is the last resort.
    bool alternative = false;
    for (const auto& c : cores)
        if (c.index != 0 && !c.smtSibling) alternative = true;
    if (alternative) {
        check(fastOn != 0 && slowOn != 0, "processor 0 is avoided when anything else exists",
              "fast " + std::to_string(fastOn) + ", slow " + std::to_string(slowOn));
    }
    platform::unpin();
}

#endif  // _WIN32

}  // namespace

int main() {
#ifndef _WIN32
    // The only implementation is the Windows one; elsewhere the header's stubs
    // report failure by design and there is nothing to measure.
    std::printf("platform pinning is implemented for Windows only; skipping\n");
    return 77;
#else
    std::printf("the measurement platform\n");
    std::printf("========================\n");
    testTopologyMatchesTheSystem();
    testPinningActuallyBinds();
    testUnpinRestoresEveryProcessor();
    testInvalidPinsAreRefused();
    testClassSelection();
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
#endif
}
