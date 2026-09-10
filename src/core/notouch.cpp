#include "mayflower/notouch.hpp"

#include "detail/fleet_counter.hpp"
#include "detail/profile_key.hpp"
#include "detail/placement_gate.hpp"
#include "detail/hashing.hpp"
#include "detail/cell_ctx.hpp"
#include "detail/entry.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mayflower {
namespace {

using detail::splitmix64;
using detail::startsHorizontal;
using detail::startsVertical;

using detail::extDigit;
using detail::FleetCounter;
using detail::fleetIndexBits;
using detail::kExtBits;
using detail::kVremBits;
using detail::setExtDigit;
using detail::setVremAt;
using detail::vremAt;

// Field offsets inside the packed key. The widths are shared and live in
// detail/profile_key.hpp; the column word and the carry bit sitting between ext
// and vrem are this sweep's alone, which is why vremShift is not kExtBits * H.
struct Layout {
    int height = 0;
    int colShift = 0;
    int carryShift = 0;
    int vremShift = 0;
    int fleetShift = 0;
    int bits = 0;
    std::uint64_t extMask = 0;
    std::uint64_t belowFleet = 0;

    Layout(const Instance& inst, const FleetCounter& fc) {
        height = inst.height;
        colShift = kExtBits * height;
        carryShift = colShift + height;
        vremShift = carryShift + 1;
        fleetShift = vremShift + kVremBits;
        bits = fleetShift + fleetIndexBits(fc.stateCount);
        // The masks are only meaningful for a key that fits, and callers ask
        // whether it fits by building this and reading `bits`. Height 20 is a
        // legal instance and puts fleetShift at 84, so an unguarded shift here
        // is undefined behaviour reached while answering the question that
        // exists to avoid it. `bits` is computed above and stays right.
        extMask = colShift >= 64 ? ~std::uint64_t{0}
                                 : (std::uint64_t{1} << colShift) - 1;
        belowFleet = fleetShift >= 64 ? ~std::uint64_t{0}
                                      : (std::uint64_t{1} << fleetShift) - 1;
    }

    [[nodiscard]] int ext(std::uint64_t k, int row) const { return extDigit(k, row); }
    [[nodiscard]] std::uint64_t setExt(std::uint64_t k, int row, int v) const {
        return setExtDigit(k, row, v);
    }
    [[nodiscard]] bool colBit(std::uint64_t k, int row) const {
        return ((k >> (colShift + row)) & 1u) != 0;
    }
    [[nodiscard]] std::uint64_t setColBit(std::uint64_t k, int row, bool b) const {
        const std::uint64_t m = std::uint64_t{1} << (colShift + row);
        return b ? (k | m) : (k & ~m);
    }
    [[nodiscard]] bool carry(std::uint64_t k) const {
        return ((k >> carryShift) & 1u) != 0;
    }
    [[nodiscard]] std::uint64_t setCarry(std::uint64_t k, bool b) const {
        const std::uint64_t m = std::uint64_t{1} << carryShift;
        return b ? (k | m) : (k & ~m);
    }
    [[nodiscard]] int vrem(std::uint64_t k) const { return vremAt(k, vremShift); }
    [[nodiscard]] std::uint64_t setVrem(std::uint64_t k, int v) const {
        return setVremAt(k, vremShift, v);
    }
    [[nodiscard]] int fleet(std::uint64_t k) const {
        return static_cast<int>(k >> fleetShift);
    }
    [[nodiscard]] std::uint64_t setFleet(std::uint64_t k, int f) const {
        return (k & belowFleet) | (static_cast<std::uint64_t>(f) << fleetShift);
    }
};

// Flat open-addressed map over the packed key. Same shape as ProfileMap, with
// the key already a uint64 so the probe needs no combining step.
class KeyMap {
public:
    explicit KeyMap(std::size_t capacityPow2 = 1024) { reserve(capacityPow2); }

    void reserve(std::size_t capacityPow2) {
        capacity_ = 1;
        while (capacity_ < capacityPow2) capacity_ <<= 1;
        mask_ = capacity_ - 1;
        keys_.assign(capacity_, 0);
        vals_.assign(capacity_, 0);
        used_.assign(capacity_, false);
        dense_.clear();
        dense_.reserve(capacityPow2);
    }

    void clear() {
        for (std::size_t slot : dense_) used_[slot] = false;
        dense_.clear();
    }

    [[nodiscard]] std::size_t size() const { return dense_.size(); }

    void add(std::uint64_t key, std::uint64_t count) {
        std::size_t slot = splitmix64(key) & mask_;
        while (true) {
            if (!used_[slot]) {
                if (dense_.size() * 10 >= capacity_ * 7) {
                    grow();
                    add(key, count);
                    return;
                }
                used_[slot] = true;
                keys_[slot] = key;
                vals_[slot] = count;
                dense_.push_back(slot);
                return;
            }
            if (keys_[slot] == key) {
                vals_[slot] += count;
                return;
            }
            slot = (slot + 1) & mask_;
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (std::size_t slot : dense_) fn(keys_[slot], vals_[slot]);
    }

private:
    void grow() {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> old;
        old.reserve(dense_.size());
        for (std::size_t slot : dense_) old.emplace_back(keys_[slot], vals_[slot]);
        reserve(capacity_ * 2);
        for (const auto& e : old) add(e.first, e.second);
    }

    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::vector<std::uint64_t> keys_;
    std::vector<std::uint64_t> vals_;
    std::vector<bool> used_;
    std::vector<std::size_t> dense_;
};

using detail::CellCtx;
using detail::makeCtx;

// The transition relation. See notouch.hpp for the neighbour table this encodes.
template <typename Emit>
inline void transitions(std::uint64_t key, const CellCtx& ctx, const FleetCounter& fc,
                        const Layout& lay, int W, int H, Emit&& emit) {
    const int r = ctx.row;
    const int d = lay.ext(key, r);
    const int vrem = lay.vrem(key);
    const int fleet = lay.fleet(key);

    const bool nw = lay.carry(key);                              // (r-1, c-1)
    const bool w = lay.colBit(key, r);                           // (r,   c-1)
    const bool sw = (r + 1 < H) && lay.colBit(key, r + 1);       // (r+1, c-1)
    const bool n = (r > 0) && lay.colBit(key, r - 1);            // (r-1, c)

    // prev[r] is about to be overwritten by cur[r], so it moves into the carry
    // for the next row. The last row instead starts a new column, where there is
    // no row above and the carry is empty.
    const bool nextCarry = (r + 1 < H) && w;
    const auto finish = [&](std::uint64_t k, bool occupied) {
        return lay.setCarry(lay.setColBit(k, r, occupied), nextCarry);
    };

    if (d > 0) {                                   // horizontal continuation
        if (vrem > 0 || ctx.mustBeEmpty) return;
        if (nw || sw || n) return;                 // (r,c-1) is the same ship
        emit(finish(lay.setExt(key, r, d - 1), true));
        return;
    }
    if (vrem > 0) {                                // vertical continuation
        if (ctx.mustBeEmpty) return;
        if (nw || w || sw) return;                 // (r-1,c) is the same ship
        emit(finish(lay.setVrem(key, vrem - 1), true));
        return;
    }

    if (!ctx.mustBeOccupied) emit(finish(key, false));
    if (ctx.mustBeEmpty) return;

    // A ship may only start with every decided neighbour clear.
    if (nw || w || sw || n) return;

    const std::size_t nLengths = fc.lengths.size();
    for (std::size_t li = 0; li < nLengths; ++li) {
        const int L = fc.lengths[li];
        const int nf = fc.afterStarting(fleet, li);
        if (nf < 0) continue;
        if (startsHorizontal(ctx.col, L, W, ctx.allowH, li)) {
            std::uint64_t k = lay.setExt(key, r, L - 1);
            emit(finish(lay.setFleet(k, nf), true));
        }
        // A length-1 ship has one placement, not two, so only the horizontal
        // branch emits it. Real fleets start at 2 and never reach this.
        if (startsVertical(ctx.row, L, H, ctx.allowV, li)) {
            std::uint64_t k = lay.setVrem(key, L - 1);
            emit(finish(lay.setFleet(k, nf), true));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------

int noTouchKeyBits(const Instance& inst) {
    inst.validate();
    const FleetCounter fc(inst);
    return Layout(inst, fc).bits;
}

bool noTouchSupports(const Instance& inst) { return noTouchKeyBits(inst) <= 64; }

CountResult countNoTouch(const Instance& inst, const Constraints& constraints) {
    detail::checkConstraints(inst, constraints);

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);
    const Layout lay(inst, fc);
    if (lay.bits > 64)
        throw std::invalid_argument("no-touching key needs more than 64 bits");

    KeyMap cur(1024), next(1024);
    cur.add(0, 1);   // empty board, no previous column, no carry

    CountResult result;
    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            result.peakStates = std::max(result.peakStates, cur.size());
            result.stateVisits += cur.size();
            result.layerSizes.push_back(static_cast<std::uint32_t>(cur.size()));

            next.clear();
            std::uint64_t edges = 0;
            cur.forEach([&](std::uint64_t key, std::uint64_t count) {
                transitions(key, ctx, fc, lay, W, H, [&](std::uint64_t dst) {
                    next.add(dst, count);
                    ++edges;
                });
            });
            result.edges += edges;
            std::swap(cur, next);
        }
    }

    // The trailing column occupancy is a record of the last column, not part of
    // acceptance, so accepting keys differing only there are summed.
    std::uint64_t total = 0;
    cur.forEach([&](std::uint64_t key, std::uint64_t count) {
        if ((key & lay.extMask) == 0 && lay.vrem(key) == 0 && lay.fleet(key) == fc.fullIndex)
            total += count;
    });
    result.count = total;
    return result;
}

CountResult countNoTouch(const Instance& inst, const std::vector<CellConstraint>& cells) {
    Constraints c;
    c.cells = cells;
    return countNoTouch(inst, c);
}

CountResult countNoTouch(const Instance& inst) {
    Constraints c = detail::freeConstraints(inst);
    return countNoTouch(inst, c);
}

}  // namespace mayflower
