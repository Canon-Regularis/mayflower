#include "mayflower/profile_dp.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace mayflower {
namespace {

// ext packs one 3-bit residual per row (max residual maxLen-1 <= 7), so 20 rows
// need 60 bits. aux packs vrem (3 bits) and the fleet-usage index.
struct Key {
    std::uint64_t ext = 0;
    std::uint32_t aux = 0;

    friend bool operator==(const Key& a, const Key& b) {
        return a.ext == b.ext && a.aux == b.aux;
    }
};

constexpr std::uint32_t packAux(int vrem, int fleetIdx) {
    return static_cast<std::uint32_t>(vrem) | (static_cast<std::uint32_t>(fleetIdx) << 3);
}
constexpr int auxVrem(std::uint32_t aux)  { return static_cast<int>(aux & 7u); }
constexpr int auxFleet(std::uint32_t aux) { return static_cast<int>(aux >> 3); }

constexpr int extDigit(std::uint64_t ext, int row) {
    return static_cast<int>((ext >> (3 * row)) & 7u);
}

inline std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Flat open-addressed map, power-of-two capacity, linear probing, O(live) clear
// via a dense slot list. std::unordered_map costs a pointer chase per probe and
// deallocates on every layer clear.
//
// V0 rung of the optimisation ladder. Later rungs are measured against this, so
// it is a straightforward implementation and not a deliberately slow one.
class ProfileMap {
public:
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
        std::size_t slot = (mix(key.ext ^ (std::uint64_t{key.aux} * 0x9E3779B1u))) & mask_;
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

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (std::size_t slot : dense_) fn(keys_[slot], vals_[slot]);
    }

private:
    void grow() {
        std::vector<Key>           oldKeys;
        std::vector<std::uint64_t> oldVals;
        oldKeys.reserve(dense_.size());
        oldVals.reserve(dense_.size());
        for (std::size_t slot : dense_) {
            oldKeys.push_back(keys_[slot]);
            oldVals.push_back(vals_[slot]);
        }
        reserve(capacity_ * 2);
        for (std::size_t i = 0; i < oldKeys.size(); ++i) add(oldKeys[i], oldVals[i]);
    }

    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::vector<Key>           keys_;
    std::vector<std::uint64_t> vals_;
    std::vector<bool>          used_;
    std::vector<std::size_t>   dense_;
};

// Mixed-radix encoding of how many ships of each distinct length are started.
struct FleetCounter {
    std::vector<int> lengths;      // ascending distinct lengths
    std::vector<int> caps;         // multiplicity of each
    std::vector<int> radixStride;
    int stateCount = 1;
    int fullIndex = 0;
    std::vector<int> addTable;     // [state * nLengths + li] -> new state, or -1

    explicit FleetCounter(const Instance& inst)
        : lengths(inst.distinctLengths()), caps(inst.multiplicities()) {
        radixStride.resize(lengths.size());
        int stride = 1;
        for (std::size_t i = 0; i < lengths.size(); ++i) {
            radixStride[i] = stride;
            stride *= caps[i] + 1;
        }
        stateCount = stride;
        fullIndex = stateCount - 1;  // all counts at cap == last mixed-radix index

        addTable.assign(static_cast<std::size_t>(stateCount) * lengths.size(), -1);
        for (int s = 0; s < stateCount; ++s) {
            for (std::size_t li = 0; li < lengths.size(); ++li) {
                int used = (s / radixStride[li]) % (caps[li] + 1);
                addTable[static_cast<std::size_t>(s) * lengths.size() + li] =
                    (used < caps[li]) ? s + radixStride[li] : -1;
            }
        }
    }

    [[nodiscard]] int afterStarting(int state, std::size_t li) const {
        return addTable[static_cast<std::size_t>(state) * lengths.size() + li];
    }
};

}  // namespace

CountResult countConfigurations(const Instance& inst,
                                const std::vector<CellConstraint>& cells) {
    inst.validate();
    if (static_cast<int>(cells.size()) != inst.cellCount())
        throw std::invalid_argument("constraint vector size must equal cellCount()");

    const int W = inst.width;
    const int H = inst.height;
    const FleetCounter fc(inst);
    const std::size_t nLengths = fc.lengths.size();

    ProfileMap cur(1024), next(1024);
    cur.add(Key{0, packAux(0, 0)}, 1);

    CountResult result;

    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const CellConstraint constraint = cells[static_cast<std::size_t>(row * W + col)];
            const bool mustBeEmpty    = constraint == CellConstraint::MustBeEmpty;
            const bool mustBeOccupied = constraint == CellConstraint::MustBeOccupied;
            const int shift = 3 * row;

            result.peakStates = std::max(result.peakStates, cur.size());
            result.stateVisits += cur.size();

            next.clear();
            std::uint64_t edges = 0;

            cur.forEach([&](const Key& key, std::uint64_t count) {
                const int vrem = auxVrem(key.aux);
                const int fleet = auxFleet(key.aux);
                const int d = extDigit(key.ext, row);

                if (d > 0) {
                    if (vrem > 0 || mustBeEmpty) return;   // vrem > 0 would overlap
                    next.add(Key{key.ext - (std::uint64_t{1} << shift), key.aux}, count);
                    ++edges;
                    return;
                }
                if (vrem > 0) {
                    if (mustBeEmpty) return;
                    next.add(Key{key.ext, packAux(vrem - 1, fleet)}, count);
                    ++edges;
                    return;
                }
                if (!mustBeOccupied) {
                    next.add(key, count);   // leave empty
                    ++edges;
                }
                if (mustBeEmpty) return;
                for (std::size_t li = 0; li < nLengths; ++li) {
                    const int L = fc.lengths[li];
                    const int nf = fc.afterStarting(fleet, li);
                    if (nf < 0) continue;
                    if (col + L <= W) {   // start horizontal
                        next.add(Key{key.ext | (static_cast<std::uint64_t>(L - 1) << shift),
                                     packAux(0, nf)},
                                 count);
                        ++edges;
                    }
                    if (row + L <= H) {   // start vertical
                        next.add(Key{key.ext, packAux(L - 1, nf)}, count);
                        ++edges;
                    }
                }
            });

            result.edges += edges;
            std::swap(cur, next);
        }
        // row+L <= H already forces vrem == 0 at the column boundary.
    }

    std::uint64_t total = 0;
    cur.forEach([&](const Key& key, std::uint64_t count) {
        if (key.ext == 0 && auxVrem(key.aux) == 0 && auxFleet(key.aux) == fc.fullIndex)
            total += count;
    });
    result.count = total;
    return result;
}

CountResult countConfigurations(const Instance& inst) {
    std::vector<CellConstraint> free(static_cast<std::size_t>(inst.cellCount()),
                                     CellConstraint::Free);
    return countConfigurations(inst, free);
}

std::uint64_t occupancyCount(const Instance& inst, int row, int col) {
    std::vector<CellConstraint> cells(static_cast<std::size_t>(inst.cellCount()),
                                      CellConstraint::Free);
    cells[static_cast<std::size_t>(row * inst.width + col)] = CellConstraint::MustBeOccupied;
    return countConfigurations(inst, cells).count;
}

}  // namespace mayflower
