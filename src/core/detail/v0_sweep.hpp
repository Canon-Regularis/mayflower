// The V0 sweep's shared vocabulary.
//
// Four responsibilities drive off this: the forward count, the flow analysis,
// the six-outcome distribution and the sampler's unranking walk. They lived in
// one translation unit and shared it through an anonymous namespace, which is
// why that file reached 664 lines owning six things at once. Splitting the file
// means naming what they share first.
//
// transitions is the single definition of the transition relation, and the
// reason all four agree. Its emission order is fixed, which is what makes
// unranking a stable bijection rather than merely a surjection onto the right
// multiset.
//
// Internal to src/core. Nothing here appears in a public header.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "flat_layer_map.hpp"

#include "mayflower/constraints.hpp"

#include "fleet_counter.hpp"
#include "hashing.hpp"
#include "placement_gate.hpp"
#include "cell_ctx.hpp"
#include "profile_key.hpp"

namespace mayflower::detail {

// The V0 layer map: flat, open-addressed, power-of-two capacity, linear
// probing, O(live) clear via a dense slot list. std::unordered_map costs a
// pointer chase per probe and deallocates on every layer clear.
//
// V0 rung of the optimisation ladder. Later rungs are measured against this, so
// it is a straightforward implementation and not a deliberately slow one.
//
// The structure itself is in detail/flat_layer_map.hpp, shared with the
// weighted sweep and the no-touching sweep, which each carried a copy and each
// said so in a comment. What stays here is the probe: this key is two fields
// and needs combining before the mixer sees it, so KeyHash lives beside Key
// in profile_key.hpp, which the weighted sweep reaches without this header.
using ProfileMap = FlatLayerMap<Key, std::uint64_t, KeyHash>;

enum class Kind : std::uint8_t { HorizContinue, VertContinue, Empty, StartH, StartV };

// The single definition of the transition relation. Forward accumulation,
// backward relaxation, the empty-flow scan and the sampler all drive off this
// function, so they cannot disagree. The emission order is fixed, which is what
// makes unranking a stable bijection.
//
// emit(destination, kind, length)  -- length is meaningful for StartH/StartV
template <typename Emit>
inline void transitions(const Key& key, const CellCtx& ctx, const FleetCounter& fc,
                        int W, int H, Emit&& emit) {
    const int vrem  = auxVrem(key.aux);
    const int fleet = auxFleet(key.aux);
    const int d     = extDigit(key.ext, ctx.row);

    if (d > 0) {
        if (vrem > 0 || ctx.mustBeEmpty) return;   // vrem > 0 would overlap
        emit(Key{key.ext - (std::uint64_t{1} << ctx.shift), key.aux}, Kind::HorizContinue, 0);
        return;
    }
    if (vrem > 0) {
        if (ctx.mustBeEmpty) return;
        emit(Key{key.ext, packAux(vrem - 1, fleet)}, Kind::VertContinue, 0);
        return;
    }
    if (!ctx.mustBeOccupied) emit(key, Kind::Empty, 0);
    if (ctx.mustBeEmpty) return;

    const std::size_t nLengths = fc.lengths.size();
    for (std::size_t li = 0; li < nLengths; ++li) {
        const int L  = fc.lengths[li];
        const int nf = fc.afterStarting(fleet, li);
        if (nf < 0) continue;
        if (startsHorizontal(ctx.col, L, W, ctx.allowH, li)) {
            emit(Key{key.ext | (static_cast<std::uint64_t>(L - 1) << ctx.shift),
                     packAux(0, nf)},
                 Kind::StartH, L);
        }
        // A length-1 ship has one placement, not two, so only the horizontal
        // branch emits it. Real fleets start at 2 and never reach this.
        if (startsVertical(ctx.row, L, H, ctx.allowV, li)) {
            emit(Key{key.ext, packAux(L - 1, nf)}, Kind::StartV, L);
        }
    }
}

[[nodiscard]] inline bool accepting(const Key& key, const FleetCounter& fc) {
    return key.ext == 0 && auxVrem(key.aux) == 0 && auxFleet(key.aux) == fc.fullIndex;
}

// ---------------------------------------------------------------------------
// The checkpointed sweep, shared by the flow analysis and the sampler.
//
// Both need the same thing: a forward pass that keeps only the W+1 column
// boundaries, and a way to rebuild the H layers inside a column from its left
// boundary while a backward pass walks through it. That is what holds the
// working set to one column instead of the whole lattice. Both wrote it out,
// identically apart from where the accepting total was stored.
//
// src/core/profile_dp.cpp is a third shape of the same loop and is NOT a
// caller. It carries the ladder's instrumentation and the 128-bit overflow
// detector, and it is the measured V0 baseline, so it keeps its own.
// ---------------------------------------------------------------------------

using Layer = std::vector<std::pair<Key, std::uint64_t>>;

struct ForwardSweep {
    std::vector<Layer> boundary;   // W + 1 snapshots, one per column boundary
    std::uint64_t total = 0;       // |Omega| under the constraints
};

[[nodiscard]] inline ForwardSweep forwardBoundaries(const Instance& inst,
                                                    const Constraints& constraints,
                                                    const FleetCounter& fc) {
    const int W = inst.width, H = inst.height;
    ForwardSweep out;
    out.boundary.resize(static_cast<std::size_t>(W) + 1);

    ProfileMap cur(1024), next(1024);
    cur.add(Key{0, packAux(0, 0)}, 1);
    out.boundary[0] = cur.snapshot();
    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            next.clear();
            cur.forEach([&](const Key& key, std::uint64_t count) {
                transitions(key, ctx, fc, W, H,
                            [&](const Key& dst, Kind, int) { next.add(dst, count); });
            });
            std::swap(cur, next);
        }
        out.boundary[static_cast<std::size_t>(col) + 1] = cur.snapshot();
    }
    cur.forEach([&](const Key& key, std::uint64_t count) {
        if (accepting(key, fc)) out.total += count;
    });
    return out;
}

// Rebuild column `col`'s H forward layers from its left boundary. `cur` and
// `next` are the caller's scratch maps, kept across columns so the sweep does
// not reallocate per column.
inline void replayColumn(const Instance& inst, const Constraints& constraints,
                         const FleetCounter& fc, int col, const Layer& from,
                         std::vector<Layer>& fLayers, ProfileMap& cur, ProfileMap& next) {
    const int W = inst.width, H = inst.height;
    cur.load(from);
    for (int row = 0; row < H; ++row) {
        fLayers[static_cast<std::size_t>(row)] = cur.snapshot();
        const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
        next.clear();
        cur.forEach([&](const Key& key, std::uint64_t count) {
            transitions(key, ctx, fc, W, H,
                        [&](const Key& dst, Kind, int) { next.add(dst, count); });
        });
        std::swap(cur, next);
    }
}

}  // namespace mayflower::detail
