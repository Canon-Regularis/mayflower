// Placement flows and the one-ply outcome distribution.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"
#include "oracle/brute_force.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

template <typename T>
void checkEq(T got, T want, const std::string& what) {
    ++gChecks;
    if (got != want) {
        ++gFailures;
        std::printf("  FAIL  %s: got %llu, want %llu\n", what.c_str(),
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

using mayflower::History;
using mayflower::Instance;
using mayflower::OutcomeDistribution;
using mayflower::Outcome;

// Each configuration contains exactly m_L ships of length L, so the flows of all
// length-L placements sum to m_L * |Omega|. Flows of placements covering a cell
// sum to that cell's occupancy count.
void testPlacementFlowInvariants() {
    std::printf("[placement flow invariants]\n");
    struct Case { int w, h; std::vector<int> fleet; };
    const std::vector<Case> cases = {
        {4, 4, {3, 2}}, {5, 5, {3, 2, 2}}, {6, 6, {4, 3, 3, 2}}, {4, 6, {3, 2}},
    };
    for (const Case& c : cases) {
        const Instance inst(c.w, c.h, c.fleet);
        mayflower::Constraints free;
        free.cells.assign(static_cast<std::size_t>(inst.cellCount()),
                          mayflower::CellConstraint::Free);
        const auto flows = mayflower::analyse(inst, free);
        const auto lengths = inst.distinctLengths();
        const auto mult = inst.multiplicities();

        for (std::size_t li = 0; li < lengths.size(); ++li) {
            std::uint64_t sum = 0;
            for (int r = 0; r < inst.height; ++r)
                for (int cc = 0; cc < inst.width; ++cc)
                    for (int h = 0; h < 2; ++h)
                        sum += flows.placement[mayflower::placementIndex(
                            inst, r, cc, static_cast<int>(li), h == 0)];
            checkEq(sum, static_cast<std::uint64_t>(mult[li]) * flows.total,
                    inst.describe() + " length-" + std::to_string(lengths[li]) +
                        " flows sum to multiplicity * total");
        }

        for (int r = 0; r < inst.height; ++r) {
            for (int cc = 0; cc < inst.width; ++cc) {
                std::uint64_t covering = 0;
                for (std::size_t li = 0; li < lengths.size(); ++li) {
                    const int L = lengths[li];
                    for (int k = 0; k < L; ++k) {
                        const int c0 = cc - k;
                        if (c0 >= 0 && c0 + L <= inst.width)
                            covering += flows.placement[mayflower::placementIndex(
                                inst, r, c0, static_cast<int>(li), true)];
                        const int r0 = r - k;
                        if (r0 >= 0 && r0 + L <= inst.height)
                            covering += flows.placement[mayflower::placementIndex(
                                inst, r0, cc, static_cast<int>(li), false)];
                    }
                }
                checkEq(covering, flows.occupancy[static_cast<std::size_t>(r * inst.width + cc)],
                        inst.describe() + " covering flows equal occupancy");
            }
        }
        std::printf("  %-16s per-length sums and per-cell coverage both exact\n",
                    inst.describe().c_str());
    }
}

// The definitive check: replay every consistent board and tally what shooting
// each candidate cell would produce.
void testOutcomesAgainstOracle() {
    std::printf("[outcome distribution vs oracle]\n");
    const int W = 5, H = 5;
    const std::vector<int> fleet = {3, 2, 2};
    const Instance inst(W, H, fleet);
    const auto boards = oracle::enumerateBoards(W, H, fleet);

    struct Scenario { std::vector<std::tuple<int, int, Outcome, int>> shots; const char* label; };
    const std::vector<Scenario> scenarios = {
        {{}, "empty board"},
        {{{1, 1, Outcome::Hit, 0}}, "one hit"},
        {{{0, 0, Outcome::Miss, 0}, {2, 2, Outcome::Hit, 0}}, "miss then hit"},
        {{{1, 1, Outcome::Hit, 0}, {2, 1, Outcome::Hit, 0}, {3, 1, Outcome::Sunk, 3}},
         "a sunk 3-ship"},
        {{{4, 4, Outcome::Miss, 0}, {0, 2, Outcome::Hit, 0}, {0, 3, Outcome::Sunk, 2}},
         "a sunk 2-ship"},
    };

    for (const auto& sc : scenarios) {
        History h(inst);
        std::vector<int> shotCells;
        for (const auto& s : sc.shots) {
            h.add(std::get<0>(s), std::get<1>(s), std::get<2>(s), std::get<3>(s));
            shotCells.push_back(std::get<0>(s) * W + std::get<1>(s));
        }
        const auto observed = [&] {
            std::vector<oracle::Observation> o;
            for (const auto& s : sc.shots) {
                switch (std::get<2>(s)) {
                    case Outcome::Miss: o.push_back({oracle::Outcome::Miss, 0}); break;
                    case Outcome::Hit:  o.push_back({oracle::Outcome::Hit, 0});  break;
                    case Outcome::Sunk: o.push_back({oracle::Outcome::Sunk, std::get<3>(s)}); break;
                }
            }
            return o;
        }();

        std::vector<oracle::BoardShips> consistent;
        for (const auto& b : boards)
            if (oracle::simulate(b, shotCells) == observed) consistent.push_back(b);

        std::uint64_t total = 0;
        const auto dist = mayflower::outcomeDistribution(inst, h, total);
        checkEq(total, static_cast<std::uint64_t>(consistent.size()),
                std::string(sc.label) + ": |Omega|");

        for (int cell = 0; cell < W * H; ++cell) {
            const OutcomeDistribution& d = dist[static_cast<std::size_t>(cell)];
            if (h.shot(cell)) {
                check(!d.shootable, std::string(sc.label) + ": shot cells are not shootable");
                continue;
            }
            OutcomeDistribution want;
            want.shootable = true;
            std::vector<int> probe = shotCells;
            probe.push_back(cell);
            for (const auto& b : consistent) {
                const auto o = oracle::simulate(b, probe).back();
                switch (o.outcome) {
                    case oracle::Outcome::Miss: ++want.miss; break;
                    case oracle::Outcome::Hit:  ++want.hit;  break;
                    case oracle::Outcome::Sunk:
                        ++want.sunk[static_cast<std::size_t>(o.sunkLength)];
                        break;
                }
            }
            const std::string tag = std::string(sc.label) + " cell " + std::to_string(cell);
            checkEq(d.miss, want.miss, tag + " miss");
            checkEq(d.hit, want.hit, tag + " hit");
            for (std::size_t L = 0; L < d.sunk.size(); ++L)
                checkEq(d.sunk[L], want.sunk[L], tag + " sunk(" + std::to_string(L) + ")");
            checkEq(d.total(), total, tag + " outcomes partition Omega");
        }
        std::printf("  %-18s |Omega| = %6llu, all %d candidate cells exact\n", sc.label,
                    static_cast<unsigned long long>(total), W * H - static_cast<int>(shotCells.size()));
    }
}

// At turn 0 the shortest ship has length 2, so no shot can sink anything. The
// channel is binary and the information gain is the binary entropy of the
// occupancy probability, which is increasing on [0, 1/2]. Since the largest
// occupancy on the standard board is 0.2136, the information-greedy and
// probability-greedy first shots coincide, and there is a four-way tie.
void testTurnZeroChannelIsBinary() {
    std::printf("[turn 0 channel]\n");
    const Instance inst = mayflower::standardInstance();
    const History empty(inst);
    std::uint64_t total = 0;
    const auto dist = mayflower::outcomeDistribution(inst, empty, total);
    checkEq(total, mayflower::constants::kOmega0, "turn-0 |Omega|");

    double bestP = -1.0, bestIG = -1.0;
    int tiedOnP = 0, tiedOnIG = 0;
    for (int cell = 0; cell < inst.cellCount(); ++cell) {
        const auto& d = dist[static_cast<std::size_t>(cell)];
        check(d.shootable, "every cell is shootable at turn 0");
        std::uint64_t sunkTotal = 0;
        for (std::uint64_t v : d.sunk) sunkTotal += v;
        checkEq(sunkTotal, std::uint64_t{0}, "no sink is possible on the first shot");

        const double p = d.hitProbability();
        const double hb = (p > 0.0 && p < 1.0) ? -(p * std::log2(p) + (1 - p) * std::log2(1 - p)) : 0.0;
        check(std::abs(d.informationBits() - hb) < 1e-12,
              "information gain equals the binary entropy at turn 0");
        if (p > bestP + 1e-15) { bestP = p; tiedOnP = 1; }
        else if (std::abs(p - bestP) < 1e-15) ++tiedOnP;
        if (d.informationBits() > bestIG + 1e-15) { bestIG = d.informationBits(); tiedOnIG = 1; }
        else if (std::abs(d.informationBits() - bestIG) < 1e-15) ++tiedOnIG;
    }
    check(std::abs(bestP - 0.213599) < 1e-5, "best turn-0 hit probability is 0.2136");
    checkEq(tiedOnP, 4, "four cells tie on hit probability");
    checkEq(tiedOnIG, 4, "the same four tie on information gain");
    check(bestP < 0.5, "the peak stays on the increasing branch of the binary entropy");
    std::printf("  max P(hit) = %.6f over %d tied cells, IG = %.6f bits, no sinks possible\n",
                bestP, tiedOnP, bestIG);
}

// Turn 0 cannot sink anything, so the check above sees a two-symbol channel and
// would pass on a sweep that dropped SUNK announcements from the sum entirely.
// This drives the alphabet past two: once a ship is wounded a neighbour can
// miss, wound again, or sink. The entropy is recomputed here from the public
// counts, so any term left out of informationBits() shows up as a mismatch.
void testSunkOutcomesEnterTheEntropy() {
    std::printf("[sunk announcements carry information]\n");
    const mayflower::Instance inst(5, 5, {3, 2, 2});
    mayflower::History h(inst);
    h.add(2, 2, mayflower::Outcome::Hit);

    std::uint64_t total = 0;
    const auto dist = mayflower::outcomeDistribution(inst, h, total);

    int withSink = 0;
    double worst = 0.0;
    for (int c = 0; c < inst.cellCount(); ++c) {
        const auto& d = dist[static_cast<std::size_t>(c)];
        if (!d.shootable) continue;
        const std::uint64_t t = d.total();
        if (t == 0) continue;
        std::uint64_t sunkSum = 0;
        for (std::uint64_t v : d.sunk) sunkSum += v;
        if (sunkSum == 0) continue;
        ++withSink;

        double want = 0.0;
        const auto term = [&](std::uint64_t n) {
            if (n == 0 || n == t) return;
            const double q = static_cast<double>(n) / static_cast<double>(t);
            want -= q * std::log2(q);
        };
        term(d.miss);
        term(d.hit);
        for (std::uint64_t v : d.sunk) term(v);
        const double got = d.informationBits();
        worst = std::max(worst, std::abs(want - got));
    }
    check(withSink > 0, "a wounded ship makes some cell able to announce a sink");
    check(worst < 1e-12, "information gain sums over the whole outcome alphabet");
    std::printf("  %d cells can sink, largest departure %.3e bits\n", withSink, worst);
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    testPlacementFlowInvariants();
    testOutcomesAgainstOracle();
    testTurnZeroChannelIsBinary();
    testSunkOutcomesEnterTheEntropy();

    const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures, %.2f s\n", gChecks, gFailures, dt);
    return gFailures == 0 ? 0 : 1;
}
