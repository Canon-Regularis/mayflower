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

#include "mayflower/constraints.hpp"

#include "fleet_counter.hpp"
#include "hashing.hpp"
#include "placement_gate.hpp"
#include "cell_ctx.hpp"
#include "profile_key.hpp"

namespace mayflower::detail {

// Flat open-addressed map, power-of-two capacity, linear probing, O(live) clear
// via a dense slot list. std::unordered_map costs a pointer chase per probe and
// deallocates on every layer clear.
//
// V0 rung of the optimisation ladder. Later rungs are measured against this, so
// it is a straightforward implementation and not a deliberately slow one.
class ProfileMap {
public:
    ProfileMap() { reserve(16); }
    explicit ProfileMap(std::size_t capacityPow2) { reserve(capacityPow2); }

    void reserve(std::size_t capacityPow2) {
        capacity_ = 1;
        while (capacity_ < capacityPow2) capacity_ <<= 1;
        mask_ = capacity_ - 1;
        keys_.assign(capacity_, Key{});
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

    void add(const Key& key, std::uint64_t count) {
        std::size_t slot = probe(key);
        while (true) {
            if (!used_[slot]) {
                if (dense_.size() * 10 >= capacity_ * 7) {  // load factor 0.7
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

    [[nodiscard]] std::uint64_t get(const Key& key) const {
        std::size_t slot = probe(key);
        while (used_[slot]) {
            if (keys_[slot] == key) return vals_[slot];
            slot = (slot + 1) & mask_;
        }
        return 0;
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (std::size_t slot : dense_) fn(keys_[slot], vals_[slot]);
    }

    [[nodiscard]] std::vector<std::pair<Key, std::uint64_t>> snapshot() const {
        std::vector<std::pair<Key, std::uint64_t>> out;
        out.reserve(dense_.size());
        for (std::size_t slot : dense_) out.emplace_back(keys_[slot], vals_[slot]);
        return out;
    }

    void load(const std::vector<std::pair<Key, std::uint64_t>>& entries) {
        clear();
        for (const auto& e : entries) add(e.first, e.second);
    }

private:
    [[nodiscard]] std::size_t probe(const Key& key) const {
        return splitmix64(key.ext ^ (std::uint64_t{key.aux} * 0x9E3779B1u)) & mask_;
    }

    void grow() {
        const auto old = snapshot();
        reserve(capacity_ * 2);
        for (const auto& e : old) add(e.first, e.second);
    }

    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::vector<Key>           keys_;
    std::vector<std::uint64_t> vals_;
    std::vector<bool>          used_;
    std::vector<std::size_t>   dense_;
};

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

}  // namespace mayflower::detail
