// What a sweep needs to know about one cell.
//
// notouch.cpp and profile_dp_blocked.cpp carried this struct and its builder as
// twenty seven byte identical lines, and v0_sweep.hpp carried a third copy that
// differs only by the packed key's shift. The stage 3 extraction pulled the
// placement gate and the fleet counter out of those same two files and left the
// context beside them, which is why they were still duplicated.
//
// weighted.cpp keeps its own type, because it genuinely carries more: four
// resolved weight lookups, and a builder that takes the Weights to resolve them
// from. It extends this one rather than restating it.
//
// Internal to src/core.
#pragma once

#include <cstddef>
#include <cstdint>

#include "mayflower/constraints.hpp"
#include "mayflower/instance.hpp"

#include "fleet_counter.hpp"
#include "profile_key.hpp"

namespace mayflower::detail {

struct CellCtx {
    int  row = 0;
    int  col = 0;
    // Where this row's residual sits in the packed key. Meaningful to the rungs
    // whose key packs one digit per row; the others simply do not read it.
    int  shift = 0;
    bool mustBeEmpty = false;
    bool mustBeOccupied = false;
    const std::uint8_t* allowH = nullptr;   // nLengths entries, or nullptr
    const std::uint8_t* allowV = nullptr;
};

// Fill the shared part. A rung carrying more calls this and then adds its own.
inline void fillCellCtx(CellCtx& ctx, const Instance& inst, const Constraints& c,
                        const FleetCounter& fc, int row, int col) {
    const std::size_t cell = static_cast<std::size_t>(row * inst.width + col);
    ctx.row = row;
    ctx.col = col;
    ctx.shift = kExtBits * row;
    ctx.mustBeEmpty = c.cells[cell] == CellConstraint::MustBeEmpty;
    ctx.mustBeOccupied = c.cells[cell] == CellConstraint::MustBeOccupied;
    if (c.gated()) {
        const std::size_t base = cell * fc.lengths.size();
        ctx.allowH = &c.allowH[base];
        ctx.allowV = &c.allowV[base];
    }
}

[[nodiscard]] inline CellCtx makeCtx(const Instance& inst, const Constraints& c,
                                     const FleetCounter& fc, int row, int col) {
    CellCtx ctx;
    fillCellCtx(ctx, inst, c, fc, row, col);
    return ctx;
}

}  // namespace mayflower::detail
