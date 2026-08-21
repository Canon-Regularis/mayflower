// Optimisation ladder, rung V1: packed key, epoch-tagged table, pre-sized.
//
// The baseline in profile_dp.cpp is kept unchanged as V0. Both must produce
// identical counts; tests/test_ladder.cpp enforces that, and bench/dp_bench.cpp
// measures the difference.
//
// Three changes, each with a mechanism:
//
// 1. The whole state fits in one uint64. On 10x10 the profile needs 3 bits per
//    row (30), vrem needs 3, and the fleet index needs 5, so 38 bits. V0 carried
//    a 12-byte struct padded to 16 and compared two words; here the key compare
//    is a single instruction.
//
// 2. Liveness is an epoch stamped into the key's spare high bits. A slot is live
//    when its stored word carries the current epoch, so one 64-bit compare
//    settles both liveness and key equality at once. Clearing a layer is an
//    epoch increment instead of a walk, and V0's std::vector<bool> occupancy
//    array disappears along with its bit addressing. The epoch starts at 1, so a
//    zero-filled table reads as empty.
//
// 3. The table is sized once from the known peak layer, so no rehash occurs
//    mid-sweep.
//
// Slots are 16 bytes, four to a cache line, and key and count sit in the same
// line, so a probe touches one line where V0 touched three arrays.
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "mayflower/profile_dp.hpp"

namespace mayflower {
namespace {

constexpr int kEpochBits = 16;
constexpr int kBatch = 32;
constexpr std::uint64_t kInvalidDelta = ~std::uint64_t{0};

inline std::uint64_t mix(std::uint64_t x) {
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 31;
    x *= 0x94D049BB133111EBull;
    return x ^ (x >> 29);
}

class FastMap {
public:
    struct Slot {
        std::uint64_t tagged = 0;
        std::uint64_t count = 0;
    };

    void init(std::size_t wanted, int stateBits) {
        std::size_t capacity = 1024;
        while (capacity < wanted) capacity <<= 1;
        slots_.assign(capacity, Slot{});
        mask_ = capacity - 1;
        dense_.clear();
        dense_.reserve(capacity / 2);
        stateBits_ = stateBits;
        epochLimit_ = std::uint64_t{1} << (64 - stateBits);
        epoch_ = 1;
    }

    void clear() {
        dense_.clear();
        if (++epoch_ >= epochLimit_) {
            std::fill(slots_.begin(), slots_.end(), Slot{});
            epoch_ = 1;
        }
    }

    [[nodiscard]] std::size_t size() const { return dense_.size(); }

    [[nodiscard]] inline std::size_t slotFor(std::uint64_t key) const {
        return mix(key) & mask_;
    }

    inline void prefetch(std::size_t slot) const {
        __builtin_prefetch(&slots_[slot], 1, 3);
    }

    inline void add(std::uint64_t key, std::uint64_t count) {
        addAt(key, count, slotFor(key));
    }

    inline void addAt(std::uint64_t key, std::uint64_t count, std::size_t i) {
        const std::uint64_t target = key | (epoch_ << stateBits_);
        while (true) {
            Slot& slot = slots_[i];
            if (slot.tagged == target) {
                slot.count += count;
                return;
            }
            if ((slot.tagged >> stateBits_) != epoch_) {
                slot.tagged = target;
                slot.count = count;
                dense_.push_back(static_cast<std::uint32_t>(i));
                return;
            }
            i = (i + 1) & mask_;
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        const std::uint64_t keyMask = (std::uint64_t{1} << stateBits_) - 1;
        for (std::uint32_t slot : dense_) {
            const Slot& s = slots_[slot];
            fn(s.tagged & keyMask, s.count);
        }
    }

private:
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> dense_;
    std::size_t mask_ = 0;
    std::uint64_t epoch_ = 1;
    std::uint64_t epochLimit_ = 0;
    int stateBits_ = 48;
};

}  // namespace

bool fastPathSupports(const Instance& inst) {
    const std::vector<int> lengths = inst.distinctLengths();
    const std::vector<int> caps = inst.multiplicities();
    int fleetStates = 1;
    for (int c : caps) fleetStates *= c + 1;
    int fleetBits = 0;
    while ((1 << fleetBits) < fleetStates) ++fleetBits;
    const int stateBits = 3 * inst.height + 3 + fleetBits;
    return stateBits <= 64 - kEpochBits;
}

CountResult countConfigurationsFast(const Instance& inst, const Constraints& constraints,
                                    std::size_t capacityHint) {
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");
    if (!fastPathSupports(inst))
        throw std::invalid_argument("instance does not fit the packed-key fast path");

    const int W = inst.width, H = inst.height;
    const std::vector<int> lengths = inst.distinctLengths();
    const std::vector<int> caps = inst.multiplicities();
    const int nL = static_cast<int>(lengths.size());

    int fleetStates = 1;
    std::vector<int> stride(static_cast<std::size_t>(nL));
    for (int i = 0; i < nL; ++i) {
        stride[static_cast<std::size_t>(i)] = fleetStates;
        fleetStates *= caps[static_cast<std::size_t>(i)] + 1;
    }
    int fleetBits = 0;
    while ((1 << fleetBits) < fleetStates) ++fleetBits;

    const int vremShift = 3 * H;
    const int fleetShift = vremShift + 3;
    const int stateBits = fleetShift + fleetBits;
    const int fullFleet = fleetStates - 1;

    // Fleet transitions as key deltas, so starting a ship is one addition.
    std::vector<std::uint64_t> fleetDelta(static_cast<std::size_t>(fleetStates) *
                                              static_cast<std::size_t>(nL),
                                          kInvalidDelta);
    for (int f = 0; f < fleetStates; ++f) {
        for (int li = 0; li < nL; ++li) {
            const int used = (f / stride[static_cast<std::size_t>(li)]) %
                             (caps[static_cast<std::size_t>(li)] + 1);
            if (used < caps[static_cast<std::size_t>(li)])
                fleetDelta[static_cast<std::size_t>(f) * static_cast<std::size_t>(nL) +
                           static_cast<std::size_t>(li)] =
                    static_cast<std::uint64_t>(stride[static_cast<std::size_t>(li)])
                    << fleetShift;
        }
    }

    FastMap cur, next;
    const std::size_t wanted = capacityHint ? capacityHint : (1u << 20);
    cur.init(wanted, stateBits);
    next.init(wanted, stateBits);
    cur.add(0, 1);

    CountResult result;
    std::vector<std::uint64_t> startH(static_cast<std::size_t>(nL));
    std::vector<std::uint64_t> startV(static_cast<std::size_t>(nL));
    std::vector<std::uint8_t> useH(static_cast<std::size_t>(nL));
    std::vector<std::uint8_t> useV(static_cast<std::size_t>(nL));

    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const std::size_t cell = static_cast<std::size_t>(row * W + col);
            const CellConstraint cc = constraints.cells[cell];
            const bool mustBeEmpty = cc == CellConstraint::MustBeEmpty;
            const bool mustBeOccupied = cc == CellConstraint::MustBeOccupied;
            const int extShift = 3 * row;
            const std::uint64_t extUnit = std::uint64_t{1} << extShift;
            const std::uint64_t vremUnit = std::uint64_t{1} << vremShift;
            const std::uint8_t* allowH =
                constraints.gated() ? &constraints.allowH[cell * static_cast<std::size_t>(nL)]
                                    : nullptr;
            const std::uint8_t* allowV =
                constraints.gated() ? &constraints.allowV[cell * static_cast<std::size_t>(nL)]
                                    : nullptr;

            for (int li = 0; li < nL; ++li) {
                const int L = lengths[static_cast<std::size_t>(li)];
                const bool fitsH = col + L <= W;
                const bool fitsV = row + L <= H;
                useH[static_cast<std::size_t>(li)] =
                    (fitsH && (allowH == nullptr || allowH[li])) ? 1u : 0u;
                useV[static_cast<std::size_t>(li)] =
                    (fitsV && (allowV == nullptr || allowV[li])) ? 1u : 0u;
                startH[static_cast<std::size_t>(li)] =
                    static_cast<std::uint64_t>(L - 1) << extShift;
                startV[static_cast<std::size_t>(li)] =
                    static_cast<std::uint64_t>(L - 1) << vremShift;
            }

            result.peakStates = std::max(result.peakStates, cur.size());
            result.stateVisits += cur.size();
            next.clear();
            std::uint64_t edges = 0;

            // The DP is memory-latency bound: every insert probes a random slot
            // in a table far larger than L3, so each edge costs about one DRAM
            // round trip. Staging successors and prefetching their slots before
            // inserting turns serialised misses into overlapping ones, which is
            // memory-level parallelism instead of a faster hash.
            int staged = 0;
            std::uint64_t stagedKey[kBatch];
            std::uint64_t stagedCount[kBatch];
            std::size_t stagedSlot[kBatch];

            const auto flush = [&]() {
                for (int i = 0; i < staged; ++i) {
                    stagedSlot[i] = next.slotFor(stagedKey[i]);
                    next.prefetch(stagedSlot[i]);
                }
                for (int i = 0; i < staged; ++i)
                    next.addAt(stagedKey[i], stagedCount[i], stagedSlot[i]);
                staged = 0;
            };
            const auto emit = [&](std::uint64_t key, std::uint64_t count) {
                stagedKey[staged] = key;
                stagedCount[staged] = count;
                ++edges;
                if (++staged == kBatch) flush();
            };

            cur.forEach([&](std::uint64_t key, std::uint64_t count) {
                const int d = static_cast<int>((key >> extShift) & 7u);
                const int vrem = static_cast<int>((key >> vremShift) & 7u);

                if (d > 0) {
                    if (vrem > 0 || mustBeEmpty) return;
                    emit(key - extUnit, count);
                    return;
                }
                if (vrem > 0) {
                    if (mustBeEmpty) return;
                    emit(key - vremUnit, count);
                    return;
                }
                if (!mustBeOccupied) emit(key, count);
                if (mustBeEmpty) return;

                const std::size_t fleet = static_cast<std::size_t>(key >> fleetShift);
                const std::uint64_t* deltas =
                    &fleetDelta[fleet * static_cast<std::size_t>(nL)];
                for (int li = 0; li < nL; ++li) {
                    const std::uint64_t delta = deltas[li];
                    if (delta == kInvalidDelta) continue;
                    if (useH[static_cast<std::size_t>(li)])
                        emit(key + startH[static_cast<std::size_t>(li)] + delta, count);
                    if (useV[static_cast<std::size_t>(li)])
                        emit(key + startV[static_cast<std::size_t>(li)] + delta, count);
                }
            });
            flush();

            result.edges += edges;
            std::swap(cur, next);
        }
    }

    const std::uint64_t acceptTail =
        static_cast<std::uint64_t>(fullFleet) << fleetShift;
    std::uint64_t total = 0;
    cur.forEach([&](std::uint64_t key, std::uint64_t count) {
        if (key == acceptTail) total += count;
    });
    result.count = total;
    return result;
}

CountResult countConfigurationsFast(const Instance& inst) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return countConfigurationsFast(inst, c, 0);
}

}  // namespace mayflower
