// Test harness for the C++ suite.
//
// This replaces eighteen per file copies. The copies fell into two groups. One
// group used gFailures and gChecks, printed only on failure, and ended with a
// count. The other used a single failures counter, took a detail argument,
// printed one line per check, and ended with a pass or fail word. Within each
// group the copies differed only in a column width of 58, 62 or 64.
//
// Both reporting modes are kept because they suit different tests. A test that
// checks a dozen named properties prints one line per check, so the output
// records what was verified. A test that compares two sweeps over 980 inputs
// prints nothing while it passes, so a single failure is visible.
//
// Include it as tests/oracle/brute_force.hpp is included. The test targets set
// target_include_directories(<t> PRIVATE tests).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace mf::test {

// One width, so columns line up across the suite rather than per file.
inline constexpr int kLabelWidth = 62;

// Exposed because some tests do their own reporting. A try block that expects a
// throw, and prints the value received when none is thrown, is clearer written
// directly than routed through a generic helper. Those tests increment the
// counters themselves. The harness defines the counters and the output format;
// it does not require every test to use check() or expect().
inline int gChecks = 0;
inline int gFailures = 0;

// Prints one line per check, pass or fail. Use it where the passing lines record
// which properties were verified.
inline void check(bool ok, const std::string& what, const std::string& detail = "") {
    ++gChecks;
    std::printf("  %-*s %s\n", kLabelWidth, what.c_str(), ok ? "ok" : "FAILED");
    // On failure only. A diagnosis printed under a passing line is misleading.
    if (!ok) {
        ++gFailures;
        if (!detail.empty()) std::printf("      %s\n", detail.c_str());
    }
}

// Prints only on failure. Use it for bulk comparisons, where the count in the
// tail is the summary and one line per check would hide the failures.
inline void expect(bool ok, const std::string& what, const std::string& detail = "") {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL  %s\n", what.c_str());
        if (!detail.empty()) std::printf("        %s\n", detail.c_str());
    }
}

// std::to_string rather than a printf conversion specifier. The copies did not
// agree on signedness: one used %lld where the others used %llu, which prints a
// large unsigned value as a negative number.
template <typename T>
void checkEq(T got, T want, const std::string& what) {
    expect(got == want, what,
           "got " + std::to_string(got) + ", want " + std::to_string(want));
}

// splitmix64. Five test files carried this struct verbatim.
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
    // The top 53 bits as a double in [0, 1). The same conversion is written
    // out in folds.hpp, python/stats.py and three tools.
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};

// Prints the tail and returns the process exit code. Pass the elapsed seconds
// where the test measured them.
inline int report(double seconds = -1.0) {
    if (seconds >= 0.0)
        std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, seconds);
    else
        std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

}  // namespace mf::test
