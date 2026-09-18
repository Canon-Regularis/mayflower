// Forward-backward flows, marginals and the placement index.
#include "mayflower/flows.hpp"
#include "mayflower/constraints.hpp"

#include "detail/v0_sweep.hpp"
#include "detail/entry.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mayflower {
namespace {

using detail::accepting;
using detail::CellCtx;
using detail::FleetCounter;
using detail::Kind;
using detail::makeCtx;
using detail::ProfileMap;
using detail::ForwardSweep;
using detail::forwardBoundaries;
using detail::Layer;
using detail::replayColumn;
using detail::transitions;
using detail::Key;
using detail::packAux;

}  // namespace

// ---------------------------------------------------------------------------
// Forward-backward marginals.
//
// Every configuration either occupies a cell or leaves it empty, and exactly one
// transition per layer carries it. So for cell t
//
//     occupancy(t) = total - flow through the "leave empty" transition at t
//
// and the empty transition is the identity on the state, which makes the scan a
// single walk over the layer computing F[s] * B[s].
//
// F is stored only at the W+1 column boundaries and replayed inside a column
// while B walks backward through it, which keeps the working set to one column
// instead of the whole lattice.
// ---------------------------------------------------------------------------

std::size_t placementSlots(const Instance& inst) {
    return static_cast<std::size_t>(inst.cellCount()) * 2 * inst.distinctLengths().size();
}

std::size_t placementIndex(const Instance& inst, int row, int col, int lengthIndex,
                           bool horizontal) {
    const std::size_t n = inst.distinctLengths().size();
    return (static_cast<std::size_t>(row * inst.width + col) * 2 + (horizontal ? 0u : 1u)) * n +
           static_cast<std::size_t>(lengthIndex);
}

LatticeFlows analyse(const Instance& inst, const Constraints& constraints) {
    detail::checkConstraints(inst, constraints);

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);
    const std::size_t nLengths = fc.lengths.size();

    LatticeFlows out;
    out.occupancy.assign(static_cast<std::size_t>(inst.cellCount()), 0);
    out.placement.assign(placementSlots(inst), 0);

    // Forward sweep, snapshotting column boundaries.
    const ForwardSweep fwd = forwardBoundaries(inst, constraints, fc);
    const std::vector<Layer>& boundary = fwd.boundary;
    out.total = fwd.total;
    if (out.total == 0) return out;

    // Backward sweep, one column at a time.
    ProfileMap bNext(1024), bCur(1024), replayCur(1024), replayNext(1024);
    for (const auto& e : boundary[static_cast<std::size_t>(W)])
        if (accepting(e.first, fc)) bNext.add(e.first, 1);

    std::vector<Layer> fLayers(static_cast<std::size_t>(H));
    std::vector<int> lengthSlot(9, -1);
    for (std::size_t li = 0; li < nLengths; ++li)
        lengthSlot[static_cast<std::size_t>(fc.lengths[li])] = static_cast<int>(li);

    for (int col = W - 1; col >= 0; --col) {
        replayColumn(inst, constraints, fc, col, boundary[static_cast<std::size_t>(col)],
                     fLayers, replayCur, replayNext);

        for (int row = H - 1; row >= 0; --row) {
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            const Layer& F = fLayers[static_cast<std::size_t>(row)];
            const std::size_t cell = static_cast<std::size_t>(row * W + col);

            std::uint64_t emptyFlow = 0;
            bCur.clear();
            for (const auto& e : F) {
                std::uint64_t completions = 0;
                transitions(e.first, ctx, fc, W, H,
                            [&](const Key& dst, Kind kind, int len) {
                    const std::uint64_t b = bNext.get(dst);
                    completions += b;
                    if (b == 0) return;
                    if (kind == Kind::Empty) {
                        emptyFlow += e.second * b;
                    } else if (kind == Kind::StartH || kind == Kind::StartV) {
                        const int li = lengthSlot[static_cast<std::size_t>(len)];
                        out.placement[placementIndex(inst, row, col, li,
                                                     kind == Kind::StartH)] += e.second * b;
                    }
                });
                if (completions) bCur.add(e.first, completions);
            }
            out.occupancy[cell] = out.total - emptyFlow;
            std::swap(bCur, bNext);
        }
    }
    return out;
}

OccupancyMap occupancyMap(const Instance& inst, const Constraints& constraints) {
    LatticeFlows f = analyse(inst, constraints);
    return {f.total, std::move(f.occupancy)};
}

OccupancyMap occupancyMap(const Instance& inst) {
    return occupancyMap(inst, detail::freeConstraints(inst));
}

}  // namespace mayflower
