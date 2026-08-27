// report_data: run the analyses and emit the figure-data contract as JSON.
//
// The engine writes data; the renderer reads it. Nothing downstream re-derives a
// number, so a figure cannot disagree with the engine that produced it.
//
// Volume policy: aggregates for anything measured over many games (shot-count
// histograms, per-cell shot-order means), full traces only for the handful of
// showcase games that need one. A 100-cell posterior per turn per game across
// tens of thousands of games is not storable and is not stored.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "mayflower/certify.hpp"
#include "mayflower/constants.hpp"
#include "mayflower/exact_solver.hpp"
#include "mayflower/game.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/policy.hpp"
#include "mayflower/profile_dp.hpp"

namespace {

using namespace mayflower;

std::string quote(const std::string& s) { return "\"" + s + "\""; }

template <typename T>
std::string jsonArray(const std::vector<T>& v, int decimals = -1) {
    std::string out = "[";
    char buf[64];
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        if (decimals < 0) std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v[i]));
        else std::snprintf(buf, sizeof buf, "%.*f", decimals, static_cast<double>(v[i]));
        out += buf;
    }
    return out + "]";
}

std::string num(double v, int decimals = 6) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    const int games = argc > 1 ? std::atoi(argv[1]) : 20000;
    const Instance inst = standardInstance();
    namespace k = mayflower::constants;

    std::string out = "{\n";

    // ---- meta -------------------------------------------------------------
    out += "  \"meta\": {";
    out += "\"instance\": " + quote(inst.describe());
    out += ", \"omega0\": " + std::to_string(k::kOmega0);
    out += ", \"entropyBits\": " + num(std::log2(static_cast<double>(k::kOmega0)), 4);
    out += ", \"shipCells\": " + std::to_string(k::kShipCells);
    out += ", \"cells\": " + std::to_string(k::kCellCount);
    out += ", \"games\": " + std::to_string(games);
    out += "},\n";
    std::fprintf(stderr, "meta done\n");

    // ---- exact prior occupancy -------------------------------------------
    std::uint64_t total = 0;
    const std::vector<std::uint64_t> occ = occupancyMap(inst, total);
    out += "  \"prior\": {\"width\": " + std::to_string(inst.width) +
           ", \"height\": " + std::to_string(inst.height) +
           ", \"total\": " + std::to_string(total) +
           ", \"counts\": " + jsonArray(occ) + "},\n";
    std::fprintf(stderr, "prior occupancy done\n");

    // ---- lattice shape ----------------------------------------------------
    const CountResult lattice = countConfigurations(inst);
    out += "  \"lattice\": {\"edges\": " + std::to_string(lattice.edges) +
           ", \"stateVisits\": " + std::to_string(lattice.stateVisits) +
           ", \"peakStates\": " + std::to_string(lattice.peakStates) +
           ", \"layerSizes\": " + jsonArray(lattice.layerSizes) + "},\n";
    std::fprintf(stderr, "lattice done\n");

    // ---- board-size scaling ----------------------------------------------
    out += "  \"scaling\": [";
    bool firstScale = true;
    for (int n : {6, 7, 8, 9, 10, 11}) {
        const Instance scaled(n, n, {5, 4, 3, 3, 2});
        const std::uint64_t c = countConfigurations(scaled).count;
        if (!firstScale) out += ", ";
        out += "{\"n\": " + std::to_string(n) + ", \"omega\": " + std::to_string(c) + "}";
        firstScale = false;
        std::fprintf(stderr, "scaling %dx%d done\n", n, n);
    }
    out += "],\n";

    // ---- bound ladder -----------------------------------------------------
    const auto wf = waterFillingBound(inst.fleet, k::kOmega0, inst.cellCount());
    out += "  \"bounds\": {\"coverage\": " + std::to_string(k::kCoverageBound) +
           ", \"entropy\": " + num(std::log2(static_cast<double>(k::kOmega0)) / k::kMaxBitsPerShot, 4) +
           ", \"waterfilling\": " + num(wf.bound, 4) +
           ", \"transcripts\": " + std::to_string(wf.hitTranscripts) + ", \"blocking\": [";
    bool firstBeta = true;
    for (int L : {2, 3, 4, 5}) {
        const auto b = blockingNumber(inst.width, inst.height, L);
        if (!firstBeta) out += ", ";
        out += "{\"length\": " + std::to_string(L) + ", \"beta\": " + std::to_string(b.blocking) +
               ", \"freeSet\": " + std::to_string(b.largestFreeSet) + "}";
        firstBeta = false;
    }
    out += "]},\n";
    std::fprintf(stderr, "bounds done\n");

    // ---- self-play: shot-count histograms and shot-order maps -------------
    const BoardBank bank(inst, 0xA1B2C3D4u);
    std::vector<std::vector<ShipPlacement>> boards;
    boards.reserve(static_cast<std::size_t>(games));
    for (int i = 0; i < games; ++i) boards.push_back(bank.board(static_cast<std::uint64_t>(i)));

    std::vector<std::uint64_t> policySeeds(static_cast<std::size_t>(games));
    for (int i = 0; i < games; ++i) {
        std::uint64_t z = static_cast<std::uint64_t>(i) + UINT64_C(0xD1B54A32D192ED03);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        policySeeds[static_cast<std::size_t>(i)] = z ^ (z >> 31);
    }

    struct Arm { std::string name; std::unique_ptr<Policy> policy; };
    std::vector<Arm> arms;
    arms.push_back({"random", std::make_unique<RandomPolicy>()});
    arms.push_back({"parity hunt/target", std::make_unique<ParityHuntTarget>()});
    arms.push_back({"density", std::make_unique<DensityPolicy>()});

    out += "  \"policies\": [";
    bool firstArm = true;
    for (Arm& arm : arms) {
        std::vector<std::uint64_t> histogram(static_cast<std::size_t>(inst.cellCount()) + 1, 0);
        // Shot-order accumulator: total turn index at which each cell was shot,
        // and how often it was shot at all. Aggregate, so it costs 200 numbers
        // for any number of games.
        std::vector<std::uint64_t> turnSum(static_cast<std::size_t>(inst.cellCount()), 0);
        std::vector<std::uint64_t> shotCount(static_cast<std::size_t>(inst.cellCount()), 0);
        double sum = 0, sumSq = 0;

        for (int i = 0; i < games; ++i) {
            History trace(inst);
            const auto result = playGameTraced(inst, boards[static_cast<std::size_t>(i)],
                                               *arm.policy,
                                               policySeeds[static_cast<std::size_t>(i)], trace);
            ++histogram[static_cast<std::size_t>(result.shots)];
            sum += result.shots;
            sumSq += static_cast<double>(result.shots) * result.shots;
            const auto& order = trace.sequence();
            for (std::size_t t = 0; t < order.size(); ++t) {
                const std::size_t cell = static_cast<std::size_t>(order[t]);
                turnSum[cell] += t + 1;
                ++shotCount[cell];
            }
        }
        const double mean = sum / games;
        const double sd = std::sqrt(sumSq / games - mean * mean);

        std::vector<double> meanTurn(static_cast<std::size_t>(inst.cellCount()), 0.0);
        for (std::size_t c = 0; c < meanTurn.size(); ++c)
            if (shotCount[c])
                meanTurn[c] = static_cast<double>(turnSum[c]) / static_cast<double>(shotCount[c]);

        if (!firstArm) out += ", ";
        out += "\n    {\"name\": " + quote(arm.name) + ", \"mean\": " + num(mean, 4) +
               ", \"sd\": " + num(sd, 4) + ", \"ci\": " + num(1.959964 * sd / std::sqrt((double)games), 4) +
               ", \"histogram\": " + jsonArray(histogram) +
               ", \"meanTurn\": " + jsonArray(meanTurn, 3) +
               ", \"shotRate\": " + jsonArray([&] {
                   std::vector<double> r(shotCount.size());
                   for (std::size_t c = 0; c < r.size(); ++c)
                       r[c] = static_cast<double>(shotCount[c]) / games;
                   return r;
               }(), 4) + "}";
        firstArm = false;
        std::fprintf(stderr, "policy %s done: mean %.3f\n", arm.name.c_str(), mean);
    }
    out += "\n  ],\n";

    // ---- objective comparison on exactly solvable instances ---------------
    out += "  \"objectives\": [";
    struct Small { int w, h; std::vector<int> fleet; };
    const std::vector<Small> smalls = {{3, 3, {2}}, {4, 3, {2}}, {4, 4, {3}},
                                       {5, 4, {3}}, {4, 4, {2, 2}}, {4, 4, {3, 2}}};
    bool firstSmall = true;
    for (const Small& sm : smalls) {
        const Instance si(sm.w, sm.h, sm.fleet);
        ExactSolution opt;
        try { opt = solveOptimal(si); } catch (const std::exception&) { continue; }
        DensityPolicy density;
        ExactPolicy maxProb(Objective::MaxHitProbability);
        ExactPolicy maxInfo(Objective::MaxInformationGain);
        const auto d = exactPolicyExpectation(si, density);
        const auto a = exactPolicyExpectation(si, maxProb);
        const auto b = exactPolicyExpectation(si, maxInfo);
        if (!firstSmall) out += ", ";
        out += "\n    {\"instance\": " + quote(si.describe()) +
               ", \"configurations\": " + std::to_string(opt.configurations) +
               ", \"optimal\": " + num(opt.expectedShots) +
               ", \"density\": " + num(d.expectedShots) +
               ", \"maxProb\": " + num(a.expectedShots) +
               ", \"maxInfo\": " + num(b.expectedShots) + "}";
        firstSmall = false;
        std::fprintf(stderr, "objectives %s done\n", si.describe().c_str());
    }
    out += "\n  ],\n";

    // ---- blocking certificates -------------------------------------------
    // A witness set that meets every placement of a lone length-L ship, so the
    // figure can show the covering instead of asserting the number.
    out += "  \"blockingWitness\": [";
    bool firstW = true;
    for (int L : {2, 3, 4, 5}) {
        const auto found = blockingWitness(inst.width, inst.height, L);
        const auto b = blockingNumber(inst.width, inst.height, L);
        std::vector<std::uint64_t> cellsOut(found.cells.begin(), found.cells.end());
        if (!firstW) out += ", ";
        out += "\n    {\"length\": " + std::to_string(L) +
               ", \"beta\": " + std::to_string(b.blocking) +
               ", \"drawn\": " + std::to_string(found.cells.size()) +
               ", \"optimal\": " + std::string(found.optimal ? "true" : "false") +
               ", \"cells\": " + jsonArray(cellsOut) + "}";
        firstW = false;
    }
    out += "\n  ],\n";
    std::fprintf(stderr, "blocking witnesses done\n");

    // ---- the order-dependence counterexample ------------------------------
    // Two histories over the same shots in a different order, each with the
    // posterior the engine actually computes for it.
    {
        const Instance small(5, 5, {3, 2, 2});
        struct Shot { int r, c; Outcome o; int len; };
        const std::vector<std::vector<Shot>> orders = {
            {{1,1,Outcome::Hit,0},{2,1,Outcome::Hit,0},{3,3,Outcome::Miss,0},
             {1,2,Outcome::Hit,0},{2,0,Outcome::Sunk,2},{0,0,Outcome::Miss,0},
             {4,1,Outcome::Miss,0}},
            {{1,2,Outcome::Hit,0},{1,1,Outcome::Hit,0},{4,1,Outcome::Miss,0},
             {3,3,Outcome::Miss,0},{2,0,Outcome::Hit,0},{0,0,Outcome::Miss,0},
             {2,1,Outcome::Sunk,2}},
        };
        out += "  \"orderDependence\": {\"width\": 5, \"height\": 5, \"orders\": [";
        for (std::size_t oi = 0; oi < orders.size(); ++oi) {
            History h(small);
            std::vector<std::uint64_t> cellsOut, kinds;
            for (const Shot& sh : orders[oi]) {
                h.add(sh.r, sh.c, sh.o, sh.len);
                cellsOut.push_back(static_cast<std::uint64_t>(sh.r * 5 + sh.c));
                kinds.push_back(sh.o == Outcome::Miss ? 0u : (sh.o == Outcome::Hit ? 1u : 2u));
            }
            const std::uint64_t n = countConfigurations(small, constraintsFrom(small, h)).count;
            if (oi) out += ", ";
            out += "\n    {\"omega\": " + std::to_string(n) +
                   ", \"cells\": " + jsonArray(cellsOut) +
                   ", \"kinds\": " + jsonArray(kinds) + "}";
        }
        out += "\n  ]},\n";
        std::fprintf(stderr, "order-dependence done\n");
    }


    // ---- showcase game: exact posterior collapse --------------------------
    // Full trace for one game only. This is the tier that cannot scale.
    out += "  \"collapse\": [";
    bool firstGame = true;
    for (int g : {0, 1, 2}) {
        DensityPolicy policy;
        History trace(inst);
        const auto result = playGameTraced(inst, boards[static_cast<std::size_t>(g)], policy,
                                           policySeeds[static_cast<std::size_t>(g)], trace);
        // Replay the trace, recounting the posterior after each shot.
        History replay(inst);
        std::vector<std::uint64_t> sizes{k::kOmega0};
        std::vector<int> outcomes;

        // Belief frames, the tier that cannot scale: every cell marginal after
        // every shot, quantised to a byte. One game only. A frame is 100 bytes
        // and a game is about 45 of them, so the whole trace costs a few
        // kilobytes and the scrubber needs no engine to replay it.
        std::vector<int> frames;
        const bool withFrames = (g == 0);
        const auto pushFrame = [&](const Constraints& c) {
            std::uint64_t frameTotal = 0;
            const std::vector<std::uint64_t> cells = occupancyMap(inst, c, frameTotal);
            for (std::uint64_t v : cells) {
                const double p = frameTotal ? static_cast<double>(v) /
                                                  static_cast<double>(frameTotal)
                                            : 0.0;
                frames.push_back(static_cast<int>(p * 255.0 + 0.5));
            }
        };
        if (withFrames) pushFrame(constraintsFrom(inst, replay));

        for (int cell : trace.sequence()) {
            const Outcome o = trace.outcome(cell);
            replay.add(cell / inst.width, cell % inst.width, o, trace.sunkLength(cell));
            const Constraints c = constraintsFrom(inst, replay);
            sizes.push_back(countConfigurations(inst, c).count);
            outcomes.push_back(o == Outcome::Miss ? 0 : (o == Outcome::Hit ? 1 : 2));
            if (withFrames) pushFrame(c);
        }
        if (!firstGame) out += ", ";
        out += "\n    {\"game\": " + std::to_string(g) + ", \"shots\": " +
               std::to_string(result.shots) + ", \"omega\": " + jsonArray(sizes) +
               ", \"outcomes\": " + jsonArray(outcomes) +
               ", \"cells\": " + jsonArray(trace.sequence());
        if (withFrames) {
            out += ", \"truth\": " + jsonArray([&] {
                       std::vector<int> t(static_cast<std::size_t>(inst.cellCount()), 0);
                       for (const ShipPlacement& sp :
                            boards[static_cast<std::size_t>(g)])
                           for (int k = 0; k < sp.length; ++k) {
                               const int cell = sp.horizontal
                                                    ? sp.row * inst.width + sp.col + k
                                                    : (sp.row + k) * inst.width + sp.col;
                               t[static_cast<std::size_t>(cell)] = 1;
                           }
                       return t;
                   }());
            out += ", \"frames\": " + jsonArray(frames);
        }
        out += "}";
        firstGame = false;
        std::fprintf(stderr, "collapse game %d done (%d shots)\n", g, result.shots);
    }
    out += "\n  ]\n}\n";

    std::printf("%s", out.c_str());
    return 0;
}
