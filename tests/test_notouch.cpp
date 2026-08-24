// The no-touching ruleset, checked against literal enumeration.
//
// The oracle includes nothing from include/mayflower, so agreement is between
// two implementations that share no code: a sweep carrying a boundary profile,
// and a recursion that places ships and rejects the ones that touch.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/notouch.hpp"
#include "mayflower/profile_dp.hpp"
#include "oracle/brute_force.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}

struct Case {
    int w, h;
    std::vector<int> fleet;
};

const std::vector<Case>& ladder() {
    static const std::vector<Case> cases{
        {3, 3, {2}},    {4, 4, {2}},     {4, 4, {3}},       {4, 4, {4}},
        {4, 4, {2, 2}}, {4, 4, {3, 2}},  {5, 5, {3, 2}},    {5, 5, {4, 3, 2}},
        {4, 6, {3, 2}}, {6, 6, {4, 3, 2}}, {5, 5, {3, 3, 2}}, {6, 6, {4, 3, 3, 2}},
    };
    return cases;
}

void testAgainstEnumeration() {
    std::printf("[no-touching against literal enumeration]\n");
    for (const Case& c : ladder()) {
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        const std::uint64_t dp = mayflower::countNoTouch(inst).count;
        const std::uint64_t brute = oracle::bruteForceCountNoTouch(c.w, c.h, c.fleet);
        char label[96];
        std::snprintf(label, sizeof label, "%s: %llu", inst.describe().c_str(),
                      static_cast<unsigned long long>(dp));
        check(dp == brute, label);
        if (dp != brute)
            std::printf("      sweep %llu, enumeration %llu\n",
                        static_cast<unsigned long long>(dp),
                        static_cast<unsigned long long>(brute));
    }
}

void testStandardInstance() {
    std::printf("[the standard instance]\n");
    const mayflower::Instance inst;
    const mayflower::CountResult r = mayflower::countNoTouch(inst);
    check(r.count == mayflower::constants::kOmegaNoTouch,
          "10x10 {5,4,3,3,2} no-touching count matches the published constant");
    std::printf("      %llu configurations, peak %zu states, %llu edges\n",
                static_cast<unsigned long long>(r.count), r.peakStates,
                static_cast<unsigned long long>(r.edges));
}

// A lone ship cannot touch itself, so forbidding contact changes nothing.
void testSingleShipUnchanged() {
    std::printf("[a lone ship is unaffected]\n");
    for (const Case& c : ladder()) {
        if (c.fleet.size() != 1) continue;
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        check(mayflower::countNoTouch(inst).count == mayflower::countConfigurations(inst).count,
              (inst.describe() + ": both rulesets agree").c_str());
    }
}

void testNeverExceedsTouching() {
    std::printf("[forbidding contact never adds configurations]\n");
    bool ok = true, strict = false;
    for (const Case& c : ladder()) {
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        const std::uint64_t nt = mayflower::countNoTouch(inst).count;
        const std::uint64_t t = mayflower::countConfigurations(inst).count;
        if (nt > t) ok = false;
        if (nt < t) strict = true;
    }
    check(ok, "no-touching count <= touching count on every ladder case");
    check(strict, "and is strictly smaller on at least one");
}

// Constraints must cut the count the same way in both implementations.
void testConstrained() {
    std::printf("[constrained counts]\n");
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    const auto next = [&]() {
        seed += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };

    int agreed = 0;
    const int trials = 240;
    for (int t = 0; t < trials; ++t) {
        const Case& c = ladder()[next() % 8];   // the cheap end of the ladder
        const mayflower::Instance inst(c.w, c.h, c.fleet);
        const int n = inst.cellCount();

        std::vector<mayflower::CellConstraint> cells(
            static_cast<std::size_t>(n), mayflower::CellConstraint::Free);
        std::vector<int> mirror(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i) {
            const int r = static_cast<int>(next() % 6);
            if (r == 0) {
                cells[static_cast<std::size_t>(i)] = mayflower::CellConstraint::MustBeOccupied;
                mirror[static_cast<std::size_t>(i)] = 2;
            } else if (r == 1) {
                cells[static_cast<std::size_t>(i)] = mayflower::CellConstraint::MustBeEmpty;
                mirror[static_cast<std::size_t>(i)] = 1;
            }
        }

        const std::uint64_t dp = mayflower::countNoTouch(inst, cells).count;
        const std::uint64_t brute =
            oracle::bruteForceCountNoTouchConstrained(c.w, c.h, c.fleet, mirror);
        if (dp == brute) ++agreed;
        else
            std::printf("      %s trial %d: sweep %llu, enumeration %llu\n",
                        inst.describe().c_str(), t,
                        static_cast<unsigned long long>(dp),
                        static_cast<unsigned long long>(brute));
    }
    char label[96];
    std::snprintf(label, sizeof label, "%d/%d random constraint patterns agree", agreed,
                  trials);
    check(agreed == trials, label);
}

// A miss can only remove configurations.
void testMonotone() {
    std::printf("[constraints are monotone]\n");
    const mayflower::Instance inst(5, 5, {4, 3, 2});
    std::vector<mayflower::CellConstraint> cells(25, mayflower::CellConstraint::Free);
    std::uint64_t previous = mayflower::countNoTouch(inst, cells).count;
    bool ok = true;
    for (int i = 0; i < 25; ++i) {
        cells[static_cast<std::size_t>(i)] = mayflower::CellConstraint::MustBeEmpty;
        const std::uint64_t now = mayflower::countNoTouch(inst, cells).count;
        if (now > previous) ok = false;
        previous = now;
    }
    check(ok, "adding misses never raises the count");
    check(previous == 0, "an all-miss board admits nothing");
}

void testKeyBits() {
    std::printf("[key packing]\n");
    check(mayflower::noTouchSupports(mayflower::Instance()),
          "the standard instance fits the packed key");
    check(mayflower::noTouchKeyBits(mayflower::Instance()) == 49,
          "and needs 49 bits of it");
}

}  // namespace

int main() {
    std::printf("no-touching ruleset\n===================\n");
    testAgainstEnumeration();
    testSingleShipUnchanged();
    testNeverExceedsTouching();
    testConstrained();
    testMonotone();
    testKeyBits();
    testStandardInstance();
    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
