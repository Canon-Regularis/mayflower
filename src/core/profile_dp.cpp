// The V0 counting sweep.
//
// Rung V0 of the optimisation ladder, and the reference every later rung is
// measured against and must match bit for bit. It is a straightforward
// implementation rather than a deliberately slow one, because a baseline that
// is worse than it needs to be inflates every speedup measured against it.
#include "mayflower/profile_dp.hpp"

#include "detail/v0_sweep.hpp"

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
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");

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
            cur.forEach([&](const Key& key, std::uint64_t count) {
                transitions(key, ctx, fc, W, H, [&](const Key& dst, Kind, int) {
                    next.add(dst, count);
                    ++edges;
                });
            });
            result.edges += edges;
            std::swap(cur, next);
        }
    }

    std::uint64_t total = 0;
    cur.forEach([&](const Key& key, std::uint64_t count) {
        if (accepting(key, fc)) total += count;
    });
    result.count = total;
    return result;
}

CountResult countConfigurations(const Instance& inst,
                                const std::vector<CellConstraint>& cells) {
    Constraints c;
    c.cells = cells;
    return countConfigurations(inst, c);
}

CountResult countConfigurations(const Instance& inst) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return countConfigurations(inst, c);
}

std::uint64_t occupancyCount(const Instance& inst, int row, int col) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    c.cells[static_cast<std::size_t>(row * inst.width + col)] = CellConstraint::MustBeOccupied;
    return countConfigurations(inst, c).count;
}

}  // namespace mayflower
