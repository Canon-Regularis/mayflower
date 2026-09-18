// The flat open-addressed layer map, once.
//
// A DP layer is built by accumulating into a key-value map and then walked in
// insertion order, so what every sweep wants is the same structure: a
// power-of-two table with linear probing, a dense list of occupied slots so a
// clear costs the live set rather than the capacity, and a load factor of 0.7.
//
// Three copies existed, and two of them said so. src/core/weighted.cpp's
// WeightMap opened with "Same shape as ProfileMap, carrying a double instead of
// a count" and src/core/notouch.cpp's KeyMap with "Same shape as ProfileMap,
// with the key already a uint64". reserve, clear, size, the probe loop, the
// load factor and grow were line for line the same in all three; they differed
// in the value type, in the key type, and in whether the probe needed a
// combining step first.
//
// Not the V1 rung's FastMap. src/core/profile_dp_fast.cpp uses epoch tagging, a
// uint32 dense list and no growth at all, which is a different structure and is
// the optimisation V1 exists to measure. ProfileMap below IS the V0 rung's map,
// so this template is on the measured path: the ladder pins that every rung
// returns the same counts, the same edge counts and the same layer profile, and
// bench/dp_bench measures whether it still costs the same.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mayflower::detail {

// `Hash` maps a key to a size_t; the table masks it down to the capacity.
template <class Key, class Value, class Hash>
class FlatLayerMap {
public:
    FlatLayerMap() { reserve(16); }
    explicit FlatLayerMap(std::size_t capacityPow2) { reserve(capacityPow2); }

    void reserve(std::size_t capacityPow2) {
        capacity_ = 1;
        while (capacity_ < capacityPow2) capacity_ <<= 1;
        mask_ = capacity_ - 1;
        keys_.assign(capacity_, Key{});
        vals_.assign(capacity_, Value{});
        used_.assign(capacity_, false);
        dense_.clear();
        dense_.reserve(capacityPow2);
    }

    // Costs the live set, not the capacity, which is why the dense list exists.
    void clear() {
        for (std::size_t slot : dense_) used_[slot] = false;
        dense_.clear();
    }

    [[nodiscard]] std::size_t size() const { return dense_.size(); }

    // Value by value, not by reference. The counting map took its count by
    // value and the reference cost 26 extra instructions in the out-of-line
    // copy of add, for a template whose three instantiations are a uint64
    // and two doubles.
    void add(const Key& key, Value value) {
        std::size_t slot = probe(key);
        while (true) {
            if (!used_[slot]) {
                if (dense_.size() * 10 >= capacity_ * 7) {  // load factor 0.7
                    grow();
                    add(key, value);
                    return;
                }
                used_[slot] = true;
                keys_[slot] = key;
                vals_[slot] = value;
                dense_.push_back(slot);
                return;
            }
            if (keys_[slot] == key) {
                vals_[slot] += value;
                return;
            }
            slot = (slot + 1) & mask_;
        }
    }

    [[nodiscard]] Value get(const Key& key) const {
        std::size_t slot = probe(key);
        while (used_[slot]) {
            if (keys_[slot] == key) return vals_[slot];
            slot = (slot + 1) & mask_;
        }
        return Value{};
    }

    // Insertion order, which is what makes the sampler's unranking walk a
    // stable bijection rather than merely a surjection.
    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (std::size_t slot : dense_) fn(keys_[slot], vals_[slot]);
    }

    [[nodiscard]] std::vector<std::pair<Key, Value>> snapshot() const {
        std::vector<std::pair<Key, Value>> out;
        out.reserve(dense_.size());
        for (std::size_t slot : dense_) out.emplace_back(keys_[slot], vals_[slot]);
        return out;
    }

    void load(const std::vector<std::pair<Key, Value>>& entries) {
        clear();
        for (const auto& e : entries) add(e.first, e.second);
    }

protected:
    // Reachable by a derived map that needs to walk the values in place.
    // src/core/weighted.cpp rescales a whole layer by a power of two and takes
    // its maximum and its sum, none of which mean anything for a counting map.
    std::vector<Value>& values() { return vals_; }
    [[nodiscard]] const std::vector<Value>& values() const { return vals_; }
    [[nodiscard]] const std::vector<std::size_t>& occupied() const { return dense_; }

private:
    [[nodiscard]] std::size_t probe(const Key& key) const { return Hash{}(key) & mask_; }

    void grow() {
        const auto old = snapshot();
        reserve(capacity_ * 2);
        for (const auto& e : old) add(e.first, e.second);
    }

    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::vector<Key>         keys_;
    std::vector<Value>       vals_;
    std::vector<bool>        used_;
    std::vector<std::size_t> dense_;
};

}  // namespace mayflower::detail
