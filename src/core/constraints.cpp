// Turning an observation record into what the sweep consumes.
//
// constraintsFrom is the only place the ordered record meets the DP. It
// produces two things: a per-cell constraint, and a per-placement gate saying
// whether a ship of a given length may start at a given cell facing a given
// way. The gate is what carries the order dependence into a sweep that has no
// notion of time.
#include "mayflower/profile_dp.hpp"

#include "detail/v0_sweep.hpp"

#include <vector>

namespace mayflower {

// ---------------------------------------------------------------------------

Constraints constraintsFrom(const Instance& inst, const History& history) {
    inst.validate();
    const int W = inst.width, H = inst.height;
    const std::vector<int> lengths = inst.distinctLengths();
    const std::size_t nLengths = lengths.size();
    const std::size_t cells = static_cast<std::size_t>(inst.cellCount());

    Constraints c;
    c.cells.assign(cells, CellConstraint::Free);
    for (std::size_t i = 0; i < cells; ++i) {
        if (!history.shot(static_cast<int>(i))) continue;
        c.cells[i] = history.outcome(static_cast<int>(i)) == Outcome::Miss
                         ? CellConstraint::MustBeEmpty
                         : CellConstraint::MustBeOccupied;
    }

    c.allowH.assign(cells * nLengths, 0);
    c.allowV.assign(cells * nLengths, 0);
    int footprint[8];
    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            const std::size_t cell = static_cast<std::size_t>(row * W + col);
            for (std::size_t li = 0; li < nLengths; ++li) {
                const int L = lengths[li];
                if (col + L <= W) {
                    for (int k = 0; k < L; ++k) footprint[k] = row * W + col + k;
                    c.allowH[cell * nLengths + li] =
                        history.allowsPlacement(footprint, L) ? 1u : 0u;
                }
                if (row + L <= H) {
                    for (int k = 0; k < L; ++k) footprint[k] = (row + k) * W + col;
                    c.allowV[cell * nLengths + li] =
                        history.allowsPlacement(footprint, L) ? 1u : 0u;
                }
            }
        }
    }
    return c;
}

}  // namespace mayflower
