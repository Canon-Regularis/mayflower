// Board-generator uniformity at full scale.
//
// tests/test_sampler.cpp proves the unranker is a bijection by enumerating every
// rank, which is the strongest possible statement and only possible on small
// boards. It says nothing about the standard instance, where the chain is
// gameId -> splitmix64 with rejection -> rank -> unrank -> placements, and where
// a bias would poison every measured statistic in the project without being
// visible anywhere else.
//
// This draws boards at scale and compares the empirical cell marginals against
// the exact ones the DP computes. The tolerance is derived rather than chosen: a
// cell with true marginal p appears Binomial(n, p) times, so the standard error
// is sqrt(p(1-p)/n) and a five-sigma band leaves a family-wise false alarm rate
// around 6e-5 across a hundred cells.
//
// The cells are not independent, since every board contributes exactly 17 of
// them, so the per-cell z scores are reported rather than combined into a single
// chi-squared that would need that independence.
//
// Nightly, not fast: the exact marginals cost a full forward-backward pass and
// the draw costs a second per few thousand boards.
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "mayflower/constants.hpp"
#include "mayflower/game.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/random.hpp"

#include "harness.hpp"

namespace {

using namespace mayflower;

using mf::test::check;

}  // namespace

int main(int argc, char** argv) {
    const int draws = argc > 1 ? std::atoi(argv[1]) : 300000;
    const Instance inst = standardInstance();
    const int cells = inst.cellCount();

    std::printf("board generator uniformity\n==========================\n");
    std::printf("  %s, %d boards\n\n", inst.describe().c_str(), draws);

    // The truth to compare against, exact and from a different code path.
    std::uint64_t total = 0;
    const std::vector<std::uint64_t> exact = occupancyMap(inst, total);
    std::vector<double> p(static_cast<std::size_t>(cells), 0.0);
    for (int c = 0; c < cells; ++c)
        p[static_cast<std::size_t>(c)] =
            static_cast<double>(exact[static_cast<std::size_t>(c)]) /
            static_cast<double>(total);

    const BoardBank bank(inst, 0xA1B2C3D4u);
    std::vector<std::uint64_t> seen(static_cast<std::size_t>(cells), 0);
    bool everyBoardWellFormed = true;
    std::uint64_t badFleet = 0;

    const std::vector<int> want = [&] {
        std::vector<int> f = inst.fleet;
        std::sort(f.begin(), f.end());
        return f;
    }();

    for (int i = 0; i < draws; ++i) {
        const auto board = bank.board(static_cast<std::uint64_t>(i));
        int occupied = 0;
        std::vector<int> lengths;
        for (const ShipPlacement& s : board) {
            lengths.push_back(s.length);
            for (int k = 0; k < s.length; ++k) {
                const int cell = s.horizontal ? s.row * inst.width + s.col + k
                                              : (s.row + k) * inst.width + s.col;
                ++seen[static_cast<std::size_t>(cell)];
                ++occupied;
            }
        }
        std::sort(lengths.begin(), lengths.end());
        if (lengths != want) { ++badFleet; everyBoardWellFormed = false; }
        if (occupied != constants::kShipCells) everyBoardWellFormed = false;
    }

    // Structure first: a board that is not a legal fleet makes the rest moot.
    char buf[160];
    std::snprintf(buf, sizeof buf, "%llu boards carried the wrong fleet",
                  static_cast<unsigned long long>(badFleet));
    check(everyBoardWellFormed, "every drawn board is the right fleet on 17 cells", buf);

    // Then the distribution.
    double worstZ = 0;
    int worstCell = -1;
    for (int c = 0; c < cells; ++c) {
        const double pc = p[static_cast<std::size_t>(c)];
        const double se = std::sqrt(pc * (1 - pc) / draws);
        const double observed =
            static_cast<double>(seen[static_cast<std::size_t>(c)]) / draws;
        const double z = se > 0 ? (observed - pc) / se : 0.0;
        if (std::abs(z) > std::abs(worstZ)) { worstZ = z; worstCell = c; }
    }
    std::snprintf(buf, sizeof buf,
                  "largest deviation %+.2f sigma at cell %d (row %d, col %d)", worstZ,
                  worstCell, worstCell / inst.width, worstCell % inst.width);
    check(std::abs(worstZ) < 5.0, "every cell within five sigma of the exact marginal",
          buf);

    // The mean over cells has to land on the ship-cell count exactly, since it
    // is a property of every individual board rather than of the distribution.
    std::uint64_t totalSeen = 0;
    for (std::uint64_t v : seen) totalSeen += v;
    std::snprintf(buf, sizeof buf, "%llu cell-hits over %d boards, expected %d per board",
                  static_cast<unsigned long long>(totalSeen), draws,
                  constants::kShipCells);
    check(totalSeen == static_cast<std::uint64_t>(draws) * constants::kShipCells,
          "occupied cells sum to 17 per board exactly", buf);

    // D4 symmetry: the prior is invariant, so orbit-mates should agree within
    // noise. This catches a bias that happens to preserve the per-cell mean.
    double worstOrbit = 0;
    for (int r = 0; r < inst.height; ++r)
        for (int c = 0; c < inst.width; ++c) {
            const int a = r * inst.width + c;
            const int b = c * inst.width + r;   // transpose, one D4 generator
            if (a >= b) continue;
            const double pa = static_cast<double>(seen[static_cast<std::size_t>(a)]) / draws;
            const double pb = static_cast<double>(seen[static_cast<std::size_t>(b)]) / draws;
            const double se = std::sqrt(2 * p[static_cast<std::size_t>(a)] *
                                        (1 - p[static_cast<std::size_t>(a)]) / draws);
            const double z = se > 0 ? (pa - pb) / se : 0.0;
            worstOrbit = std::max(worstOrbit, std::abs(z));
        }
    std::snprintf(buf, sizeof buf, "largest transpose asymmetry %.2f sigma", worstOrbit);
    check(worstOrbit < 5.0, "the draw respects the board's transpose symmetry", buf);

    // The stream itself, pinned.
    //
    // Everything above is a property that holds for any decent generator: the
    // boards are well formed, every cell sits within five sigma of its exact
    // marginal, the draw respects the transpose symmetry. So a change to the
    // hash gives different draws that are just as uniform, the whole suite stays
    // green, and every sampled number in the report has moved. Four planted
    // faults in random.hpp survived in exactly that way.
    //
    // Reproducibility is the property at risk here, not uniformity. Common
    // random numbers only work if one seed gives one sequence tomorrow, and
    // web/pool.bin is a committed artefact this stream produced. So the values
    // are pinned the way folds.hpp's vector is, and for the same reason: two
    // things that must agree forever need a witness.
    {
        std::printf("[the stream is reproducible]\n");
        const std::uint64_t pinned[8] = {
            16294208416658607535ull, 7960286522194355700ull, 487617019471545679ull,
            17909611376780542444ull, 1961750202426094747ull, 6038094601263162090ull,
            3207296026000306913ull, 14232521865600346940ull};
        mayflower::Rng r(0);
        bool same = true;
        for (const std::uint64_t expected : pinned) same = same && r.next() == expected;
        check(same, "Rng(0) reproduces its first eight draws");

        check(mayflower::keyedSeed(0, mayflower::kPolicyStreamKey) == 9370218965779684112ull,
              "the policy stream key derives the same first seed");

        char pin[64];
        std::snprintf(pin, sizeof pin, "%.17g", mayflower::Rng(0).unit());
        check(std::string(pin) == "0.88331080821364261",
              "and unit() matches to the last digit", pin);

        // The draw that fills web/pool.bin, which is committed and decoded by
        // the browser widget.
        check(mayflower::Rng(1).belowUnbiased(15046987768ull) == 6252514233ull,
              "the unbiased draw over the whole space is unchanged");

        // That draw does not reach the rejection branch, and pinning a value
        // that never executes the code it is meant to protect proves nothing:
        // deleting the loop left the case above passing. Over the full space
        // the ragged tail is about 8e-10 of the range, so a draw lands in it
        // roughly once in a billion.
        //
        // A bound just above 2^63 makes the tail half the range instead. Seed
        // 0's first draw is 16294208416658607535, above the limit of
        // 9223372036854775808, so a correct implementation discards it and
        // returns the second. One that takes the remainder of the first
        // returns 7070836379803831726 and is caught here.
        check(mayflower::Rng(0).belowUnbiased((1ull << 63) + 1) == 7960286522194355700ull,
              "and a draw in the ragged tail is rejected rather than folded");
    }

    return mf::test::report();
}
