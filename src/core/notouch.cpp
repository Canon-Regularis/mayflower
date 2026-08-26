#include "mayflower/notouch.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mayflower {
namespace {

inline std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Mirrors the counter in profile_dp.cpp. Decrementing at ship START keeps ship
// identity out of the profile, so indistinguishable lengths need no division.
struct FleetCounter {
    std::vector<int> lengths;
    std::vector<int> caps;
    std::vector<int> radixStride;
    int stateCount = 1;
    int fullIndex = 0;
    std::vector<int> addTable;

    explicit FleetCounter(const Instance& inst)
        : lengths(inst.distinctLengths()), caps(inst.multiplicities()) {
        radixStride.resize(lengths.size());
        int stride = 1;
        for (std::size_t i = 0; i < lengths.size(); ++i) {
            radixStride[i] = stride;
            stride *= caps[i] + 1;
        }
        stateCount = stride;
        fullIndex = stateCount - 1;

        addTable.assign(static_cast<std::size_t>(stateCount) * lengths.size(), -1);
        for (int s = 0; s < stateCount; ++s)
            for (std::size_t li = 0; li < lengths.size(); ++li) {
                const int used = (s / radixStride[li]) % (caps[li] + 1);
                addTable[static_cast<std::size_t>(s) * lengths.size() + li] =
                    (used < caps[li]) ? s + radixStride[li] : -1;
            }
    }

    [[nodiscard]] int afterStarting(int state, std::size_t li) const {
        return addTable[static_cast<std::size_t>(state) * lengths.size() + li];
    }
};

// Field offsets inside the packed key.
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
        colShift = 3 * height;
        carryShift = colShift + height;
        vremShift = carryShift + 1;
        fleetShift = vremShift + 3;
        const int fleetBits =
            fc.stateCount <= 1
                ? 0
                : 64 - std::countl_zero(static_cast<std::uint64_t>(fc.stateCount - 1));
        bits = fleetShift + fleetBits;
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

    [[nodiscard]] int ext(std::uint64_t k, int row) const {
        return static_cast<int>((k >> (3 * row)) & 7u);
    }
    [[nodiscard]] std::uint64_t setExt(std::uint64_t k, int row, int v) const {
        const int sh = 3 * row;
        return (k & ~(std::uint64_t{7} << sh)) | (static_cast<std::uint64_t>(v) << sh);
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
    [[nodiscard]] int vrem(std::uint64_t k) const {
        return static_cast<int>((k >> vremShift) & 7u);
    }
    [[nodiscard]] std::uint64_t setVrem(std::uint64_t k, int v) const {
        return (k & ~(std::uint64_t{7} << vremShift)) |
               (static_cast<std::uint64_t>(v) << vremShift);
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
        std::size_t slot = mix(key) & mask_;
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

struct CellCtx {
    int row = 0;
    int col = 0;
    bool mustBeEmpty = false;
    bool mustBeOccupied = false;
    const std::uint8_t* allowH = nullptr;
    const std::uint8_t* allowV = nullptr;
};

CellCtx makeCtx(const Instance& inst, const Constraints& c, const FleetCounter& fc,
                int row, int col) {
    const std::size_t cell = static_cast<std::size_t>(row * inst.width + col);
    CellCtx ctx;
    ctx.row = row;
    ctx.col = col;
    ctx.mustBeEmpty = c.cells[cell] == CellConstraint::MustBeEmpty;
    ctx.mustBeOccupied = c.cells[cell] == CellConstraint::MustBeOccupied;
    if (c.gated()) {
        const std::size_t base = cell * fc.lengths.size();
        ctx.allowH = &c.allowH[base];
        ctx.allowV = &c.allowV[base];
    }
    return ctx;
}

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
        if (ctx.col + L <= W && (ctx.allowH == nullptr || ctx.allowH[li])) {
            std::uint64_t k = lay.setExt(key, r, L - 1);
            emit(finish(lay.setFleet(k, nf), true));
        }
        // A length-1 ship has one placement, not two, so only the horizontal
        // branch emits it. Real fleets start at 2 and never reach this.
        if (L > 1 && ctx.row + L <= H && (ctx.allowV == nullptr || ctx.allowV[li])) {
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
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");

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
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return countNoTouch(inst, c);
}

}  // namespace mayflower
