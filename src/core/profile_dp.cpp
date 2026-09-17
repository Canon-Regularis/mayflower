// The V0 counting sweep.
//
// Rung V0 of the optimisation ladder, and the reference every later rung is
// measured against and must match bit for bit. It is a straightforward
// implementation rather than a deliberately slow one, because a baseline that
// is worse than it needs to be inflates every speedup measured against it.
#include "mayflower/profile_dp.hpp"

#include "detail/v0_sweep.hpp"
#include "detail/entry.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace mayflower {
namespace {

using detail::accepting;
using detail::CellCtx;
using detail::FleetCounter;
using detail::Kind;
using detail::makeCtx;
using detail::ProfileMap;
using detail::transitions;
using detail::Key;
using detail::packAux;

}  // namespace

CountResult countConfigurations(const Instance& inst, const Constraints& constraints) {
    detail::checkConstraints(inst, constraints);

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);

    ProfileMap cur(1024), next(1024);
    cur.add(Key{0, packAux(0, 0)}, 1);

    CountResult result;
    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            result.peakStates = std::max(result.peakStates, cur.size());
            result.stateVisits += cur.size();
            result.layerSizes.push_back(static_cast<std::uint32_t>(cur.size()));

            next.clear();
            std::uint64_t edges = 0;
            // The layer sum, in 128 bits, riding the walk the sweep already
            // makes. A state in the next layer collects at most one
            // contribution from each state in this one, so no value there can
            // exceed this sum, and a sum that stays inside 64 bits means no
            // value in the layer it feeds can have wrapped. No false
            // negatives.
            //
            // It can say inexact on an exact answer, since a layer of billions
            // of states can sum past 2^64 with every value small. That needs
            // more live states than this machine can hold, and the bias is
            // the safe way round.
            //
            // The sum accumulates during the walk that writes the next layer
            // and is tested after it, so the flag is set one layer later than
            // the wrap it describes. That is fine for a flag on the whole
            // result and would not be if it were per layer.
            //
            // Cost is one 128-bit add per state visit inside a loop that
            // already expands each state into up to seventeen transitions, so
            // it does not move bench/dp_bench.
            __uint128_t layerSum = 0;
            cur.forEach([&](const Key& key, std::uint64_t count) {
                layerSum += count;
                transitions(key, ctx, fc, W, H, [&](const Key& dst, Kind, int) {
                    next.add(dst, count);
                    ++edges;
                });
            });
            if (layerSum > static_cast<__uint128_t>(UINT64_MAX)) result.exact = false;
            result.edges += edges;
            std::swap(cur, next);
        }
    }

    // The final layer is summed the same way, since it is never fed forward and
    // so is not covered by the check above.
    __uint128_t total = 0;
    cur.forEach([&](const Key& key, std::uint64_t count) {
        if (accepting(key, fc)) total += count;
    });
    if (total > static_cast<__uint128_t>(UINT64_MAX)) result.exact = false;
    result.count = static_cast<std::uint64_t>(total);
    return result;
}

CountResult countConfigurations(const Instance& inst,
                                const std::vector<CellConstraint>& cells) {
    Constraints c;
    c.cells = cells;
    return countConfigurations(inst, c);
}

CountResult countConfigurations(const Instance& inst) {
    Constraints c = detail::freeConstraints(inst);
    return countConfigurations(inst, c);
}

std::uint64_t occupancyCount(const Instance& inst, int row, int col) {
    Constraints c = detail::freeConstraints(inst);
    c.cells[static_cast<std::size_t>(row * inst.width + col)] = CellConstraint::MustBeOccupied;
    return countConfigurations(inst, c).count;
}

}  // namespace mayflower
