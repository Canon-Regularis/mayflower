// Optimisation ladder, rungs V2 and V3: radix-partitioned merge, then threads.
//
// V1 gets its speed from a cheaper probe. What it cannot fix is where the probe
// lands: the live state set is a few hundred thousand entries, the table is
// several megabytes, and every emitted edge hashes to an unpredictable slot in
// it. The sweep is a pointer chase over a working set that does not fit L2.
//
// V2 splits the layer instead. Each cell is processed in two passes:
//
//   scatter   walk the live states, compute each destination key, and append
//             (key, count) to one of R buckets chosen by a radix of the key.
//             Writes are sequential per bucket, so this pass streams.
//
//   merge     take one bucket at a time and aggregate it with a table sized for
//             that bucket alone. A bucket holds roughly 1/R of the layer, so the
//             table is small enough to sit in L2 and the random access inside it
//             stops missing.
//
// The radix is taken from the hashed key, so buckets stay balanced even though
// raw keys are highly structured.
//
// V3 is V2 with the merge pass spread over threads. Buckets share nothing: a
// destination key belongs to exactly one bucket, decided in the scatter pass, so
// two merges never touch the same counter. Counts are integers and integer
// addition is associative, so the result does not depend on how the work is
// divided, and the ladder's bit-identical requirement is met by construction
// rather than by tolerance.
//
// A layer below kParallelFloor edges is merged serially whatever thread count
// was asked for. Threads are created per cell here, and on a small instance the
// creation cost dwarfs the merge by orders of magnitude; the floor is what keeps
// V3 from being far slower than V2 on the small end of the ladder. Reusing a
// pool across cells would remove the floor, and is the obvious next step.


#include "mayflower/profile_dp_blocked.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <memory>
#include <vector>


#include "detail/fleet_counter.hpp"
#include "detail/profile_key.hpp"
#include "detail/placement_gate.hpp"
#include "detail/hashing.hpp"
#include "detail/cell_ctx.hpp"
#include "detail/merge_pool.hpp"
#include "detail/entry.hpp"

namespace mayflower {
namespace {

using detail::splitmix64;
using detail::startsHorizontal;
using detail::startsVertical;

constexpr int kRadixBits = 6;                 // 64 buckets
constexpr std::size_t kRadix = std::size_t{1} << kRadixBits;

// Edges in a layer below which the merge stays serial. A layer that merges in
// less time than a barrier round trip gains nothing from being divided.
constexpr std::uint64_t kParallelFloor = 8000;

using detail::MergePool;

using detail::extDigit;
using detail::FleetCounter;
using detail::fleetIndexBits;
using detail::kExtBits;
using detail::kVremBits;
using detail::setExtDigit;
using detail::setVremAt;
using detail::vremAt;

// Packed key: ext (3 bits per row) | vrem (3) | fleet. The widths live in
// detail/profile_key.hpp; the offsets are this rung's.
struct Layout {
    int vremShift = 0;
    int fleetShift = 0;
    int bits = 0;
    std::uint64_t extMask = 0;

    Layout(const Instance& inst, const FleetCounter& fc) {
        vremShift = kExtBits * inst.height;
        fleetShift = vremShift + kVremBits;
        bits = fleetShift + fleetIndexBits(fc.stateCount);
        extMask = (std::uint64_t{1} << vremShift) - 1;
    }

    [[nodiscard]] int ext(std::uint64_t k, int row) const { return extDigit(k, row); }
    [[nodiscard]] std::uint64_t setExt(std::uint64_t k, int row, int v) const {
        return setExtDigit(k, row, v);
    }
    [[nodiscard]] int vrem(std::uint64_t k) const { return vremAt(k, vremShift); }
    [[nodiscard]] std::uint64_t setVrem(std::uint64_t k, int v) const {
        return setVremAt(k, vremShift, v);
    }
    [[nodiscard]] int fleet(std::uint64_t k) const {
        return static_cast<int>(k >> fleetShift);
    }
    [[nodiscard]] std::uint64_t setFleet(std::uint64_t k, int f) const {
        return (k & ((std::uint64_t{1} << fleetShift) - 1)) |
               (static_cast<std::uint64_t>(f) << fleetShift);
    }
};

struct Entry {
    std::uint64_t key;
    std::uint64_t count;
};

// One bucket's aggregation table. Open addressed, power of two, epoch-free
// because it is rebuilt per bucket and the bucket is small.
class BucketTable {
public:
    void prepare(std::size_t expected) {
        std::size_t want = 16;
        while (want < expected * 2) want <<= 1;
        if (want != capacity_) {
            capacity_ = want;
            mask_ = want - 1;
            keys_.assign(capacity_, 0);
            counts_.assign(capacity_, 0);
            used_.assign(capacity_, 0);
        } else {
            for (std::size_t slot : touched_) used_[slot] = 0;
        }
        touched_.clear();
    }

    void add(std::uint64_t key, std::uint64_t count) {
        std::size_t slot = splitmix64(key) & mask_;
        while (true) {
            if (!used_[slot]) {
                used_[slot] = 1;
                keys_[slot] = key;
                counts_[slot] = count;
                touched_.push_back(slot);
                return;
            }
            if (keys_[slot] == key) {
                counts_[slot] += count;
                return;
            }
            slot = (slot + 1) & mask_;
        }
    }

    void drainInto(std::vector<Entry>& out) const {
        for (std::size_t slot : touched_) out.push_back({keys_[slot], counts_[slot]});
    }

    [[nodiscard]] std::size_t distinct() const { return touched_.size(); }

private:
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::vector<std::uint64_t> keys_;
    std::vector<std::uint64_t> counts_;
    std::vector<std::uint8_t> used_;
    std::vector<std::size_t> touched_;
};

using detail::CellCtx;
using detail::makeCtx;

// Same relation as profile_dp.cpp, over the packed key.
template <typename Emit>
inline void transitions(std::uint64_t key, const CellCtx& ctx, const FleetCounter& fc,
                        const Layout& lay, int W, int H, Emit&& emit) {
    const int vrem = lay.vrem(key);
    const int fleet = lay.fleet(key);
    const int d = lay.ext(key, ctx.row);

    if (d > 0) {
        if (vrem > 0 || ctx.mustBeEmpty) return;
        emit(lay.setExt(key, ctx.row, d - 1));
        return;
    }
    if (vrem > 0) {
        if (ctx.mustBeEmpty) return;
        emit(lay.setVrem(key, vrem - 1));
        return;
    }
    if (!ctx.mustBeOccupied) emit(key);
    if (ctx.mustBeEmpty) return;

    const std::size_t nLengths = fc.lengths.size();
    for (std::size_t li = 0; li < nLengths; ++li) {
        const int L = fc.lengths[li];
        const int nf = fc.afterStarting(fleet, li);
        if (nf < 0) continue;
        if (startsHorizontal(ctx.col, L, W, ctx.allowH, li)) {
            std::uint64_t k = lay.setExt(key, ctx.row, L - 1);
            k = lay.setVrem(k, 0);
            emit(lay.setFleet(k, nf));
        }
        // A length-1 ship has one placement, not two, so only the horizontal
        // branch emits it. Real fleets start at 2 and never reach this.
        if (startsVertical(ctx.row, L, H, ctx.allowV, li)) {
            std::uint64_t k = lay.setVrem(key, L - 1);
            emit(lay.setFleet(k, nf));
        }
    }
}

}  // namespace

bool blockedPathSupports(const Instance& inst) {
    inst.validate();
    const FleetCounter fc(inst);
    return Layout(inst, fc).bits + kRadixBits <= 64;
}

CountResult countConfigurationsBlocked(const Instance& inst, const Constraints& constraints,
                                       int threads) {
    detail::checkConstraints(inst, constraints);
    if (!blockedPathSupports(inst))
        throw std::invalid_argument("instance does not fit the blocked path");
    if (threads < 1) threads = 1;

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);
    const Layout lay(inst, fc);

    std::vector<Entry> cur{{0, 1}};
    std::vector<std::vector<Entry>> bucket(kRadix);
    std::vector<std::vector<Entry>> merged(kRadix);
    std::vector<BucketTable> table(kRadix);

    CountResult result;

    // One job, one pool, reused for every cell. mergeRange reads whatever is in
    // the buckets at the time it runs, so nothing needs rebinding per cell.
    const auto mergeRange = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
            merged[i].clear();
            if (bucket[i].empty()) continue;
            table[i].prepare(bucket[i].size());
            for (const Entry& e : bucket[i]) table[i].add(e.key, e.count);
            merged[i].reserve(table[i].distinct());
            table[i].drainInto(merged[i]);
        }
    };
    std::unique_ptr<MergePool> pool;
    if (threads > 1)
        pool = std::make_unique<MergePool>(std::min<int>(threads, static_cast<int>(kRadix)),
                                           kRadix, mergeRange);

    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            result.peakStates = std::max(result.peakStates, cur.size());
            result.stateVisits += cur.size();
            result.layerSizes.push_back(static_cast<std::uint32_t>(cur.size()));

            for (auto& b : bucket) b.clear();

            // Scatter. Sequential appends, one stream per bucket.
            std::uint64_t edges = 0;
            for (const Entry& e : cur) {
                transitions(e.key, ctx, fc, lay, W, H, [&](std::uint64_t dst) {
                    const std::size_t which = splitmix64(dst) & (kRadix - 1);
                    bucket[which].push_back({dst, e.count});
                    ++edges;
                });
            }
            result.edges += edges;

            // Buckets partition the destination keys, so merges never touch
            // one counter and need no lock. Below the floor a barrier costs more
            // than the merge, so the layer is done in place.
            if (!pool || edges < kParallelFloor) mergeRange(0, kRadix);
            else pool->runAll();

            // Concatenate in bucket order, so the next layer is deterministic.
            cur.clear();
            for (auto& m : merged)
                cur.insert(cur.end(), m.begin(), m.end());
        }
    }

    std::uint64_t total = 0;
    for (const Entry& e : cur)
        if ((e.key & lay.extMask) == 0 && lay.vrem(e.key) == 0 &&
            lay.fleet(e.key) == fc.fullIndex)
            total += e.count;
    result.count = total;
    return result;
}

CountResult countConfigurationsBlocked(const Instance& inst,
                                       const std::vector<CellConstraint>& cells,
                                       int threads) {
    Constraints c;
    c.cells = cells;
    return countConfigurationsBlocked(inst, c, threads);
}

CountResult countConfigurationsBlocked(const Instance& inst, int threads) {
    Constraints c = detail::freeConstraints(inst);
    return countConfigurationsBlocked(inst, c, threads);
}

}  // namespace mayflower
