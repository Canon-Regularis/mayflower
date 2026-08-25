// Fold assignment, and the agreement between the two implementations of it.
//
// The harness selects boards in C++ and the analysis reads them in Python. If
// those two ever disagree about which fold a board is in, an experiment would be
// reading data it believes is sealed, and nothing would say so. The pinned
// vector below is reproduced by python/stats.py, so drift fails the build on
// both sides.
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <map>
#include <string>
#include <vector>

#include "mayflower/folds.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  %-58s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!detail.empty()) std::printf("      %s\n", detail.c_str());
    if (!ok) ++failures;
}

using namespace mayflower;

void testShares() {
    std::printf("[shares]\n");
    const int n = 200000;
    std::map<std::string, int> counts;
    for (int i = 0; i < n; ++i) counts[foldName(foldOf(static_cast<std::uint64_t>(i)))]++;

    char buf[96];
    const double train = counts["train"] / double(n);
    const double val = counts["val"] / double(n);
    const double test = counts["test"] / double(n);
    std::snprintf(buf, sizeof buf, "train %.4f, val %.4f, test %.4f", train, val, test);
    check(std::abs(train - 0.60) < 0.005 && std::abs(val - 0.20) < 0.005 &&
              std::abs(test - 0.20) < 0.005,
          "60 / 20 / 20 within half a percent", buf);
    check(counts["train"] + counts["val"] + counts["test"] == n,
          "every board lands in exactly one fold");
}

// The pinned vector. python/stats.py must produce the same string.
void testPinned() {
    std::printf("[cross-language agreement]\n");
    std::string got;
    for (std::uint64_t i = 0; i < 40; ++i) got += foldName(foldOf(i))[0];
    // python/stats.py pins the same two values and asserts on them, so a change
    // to either implementation fails on both sides rather than drifting quietly.
    const std::string pinned = "vvttttvvtttttvvtttttvtvtttttttvvttttttvt";
    std::printf("      first 40 folds: %s\n", got.c_str());
    check(got == pinned, "the first forty ids match the pinned vector");

    char buf[96];
    std::snprintf(buf, sizeof buf, "%.17g", foldFraction(0));
    std::printf("      foldFraction(0) = %s\n", buf);
    check(std::string(buf) == "0.78164751589525394", "and foldFraction(0) matches to the last digit");
}

void testStability() {
    std::printf("[stability]\n");
    // Thresholding a hash means a board only moves if a boundary crosses it.
    // Every train board sits below 0.60 whatever happens to the val boundary.
    int wrong = 0;
    for (std::uint64_t i = 0; i < 20000; ++i) {
        const Fold f = foldOf(i);
        const double u = foldFraction(i);
        if (f == Fold::Train && !(u < kTrainShare)) ++wrong;
        if (f == Fold::Val && !(u >= kTrainShare && u < kTrainShare + kValShare)) ++wrong;
        if (f == Fold::Test && !(u >= kTrainShare + kValShare)) ++wrong;
    }
    check(wrong == 0, "fold membership is exactly the threshold on the fraction");

    // Neighbouring ids must not correlate, which a modulus would not give.
    int adjacent = 0;
    for (std::uint64_t i = 1; i < 20000; ++i)
        if (foldOf(i) == foldOf(i - 1)) ++adjacent;
    const double expected = 19999 * (0.36 + 0.04 + 0.04);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%d adjacent matches, expected about %.0f", adjacent,
                  expected);
    check(std::abs(adjacent - expected) < 0.06 * expected,
          "neighbouring ids are independent", buf);
}

void testNames() {
    std::printf("[names]\n");
    check(foldFromName("train") == Fold::Train && foldFromName("val") == Fold::Val &&
              foldFromName("test") == Fold::Test,
          "the three names parse");
    bool threw = false;
    try {
        foldFromName("Train");
    } catch (const std::exception&) { threw = true; }
    check(threw, "a typo is refused rather than widening the pool");
}

}  // namespace

int main() {
    std::printf("fold assignment\n===============\n");
    testShares();
    testPinned();
    testStability();
    testNames();
    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
