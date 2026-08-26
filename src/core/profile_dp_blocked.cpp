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
#include <bit>
#include <stdexcept>
#include <thread>
#include <memory>
#include <vector>

#include "mayflower/platform.hpp"

namespace mayflower {
namespace {

inline std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

constexpr int kRadixBits = 6;                 // 64 buckets
constexpr std::size_t kRadix = std::size_t{1} << kRadixBits;

// Edges in a layer below which the merge stays serial. A layer that merges in
// less time than a barrier round trip gains nothing from being divided.
constexpr std::uint64_t kParallelFloor = 8000;

// Workers created once per sweep rather than once per cell.
//
// The first version of this rung spawned a thread per bucket range per cell.
// Thread creation measured several milliseconds here, and a sweep has a hundred
// cells, so V3 was reliably SLOWER than V2: the standard instance scaled to
// 0.56x at ten threads, and 6x6 took 4.5 s where one thread took 0.008. The
// work was never the problem. The pool below creates each worker once, and every
// cell costs one barrier instead of a fresh set of threads.
//
// Each worker owns a fixed slice of the bucket range for the whole sweep, so
// there is no queue and no stealing, and no two workers ever touch one bucket.
class MergePool {
public:
    MergePool(int workers, std::function<void(std::size_t, std::size_t)> job)
        : job_(std::move(job)) {
        for (int t = 0; t < workers; ++t) {
            const std::size_t from = kRadix * static_cast<std::size_t>(t) /
                                     static_cast<std::size_t>(workers);
            const std::size_t to = kRadix * static_cast<std::size_t>(t + 1) /
                                   static_cast<std::size_t>(workers);
            threads_.emplace_back([this, from, to] { loop(from, to); });
        }
        live_ = workers;
    }

    ~MergePool() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
            ++generation_;
        }
        work_.notify_all();
        for (auto& t : threads_) t.join();
    }

    // Run the job over every slice and return once all of them are finished.
    void runAll() {
        {
            std::lock_guard<std::mutex> lock(m_);
            pending_ = live_;
            ++generation_;
        }
        work_.notify_all();
        std::unique_lock<std::mutex> lock(m_);
        done_.wait(lock, [this] { return pending_ == 0; });
    }

private:
    void loop(std::size_t from, std::size_t to) {
        std::uint64_t seen = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(m_);
                work_.wait(lock, [this, seen] { return generation_ != seen; });
                seen = generation_;
                if (stop_) return;
            }
            job_(from, to);
            {
                std::lock_guard<std::mutex> lock(m_);
                --pending_;
            }
            done_.notify_one();
        }
    }

    std::function<void(std::size_t, std::size_t)> job_;
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable work_, done_;
    std::uint64_t generation_ = 0;
    int pending_ = 0;
    int live_ = 0;
    bool stop_ = false;
};

// Mixed-radix fleet counter, matching profile_dp.cpp exactly.
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

// Packed key: ext (3 bits per row) | vrem (3) | fleet.
struct Layout {
    int vremShift = 0;
    int fleetShift = 0;
    int bits = 0;
    std::uint64_t extMask = 0;

    Layout(const Instance& inst, const FleetCounter& fc) {
        vremShift = 3 * inst.height;
        fleetShift = vremShift + 3;
        const int fleetBits =
            fc.stateCount <= 1
                ? 0
                : 64 - std::countl_zero(static_cast<std::uint64_t>(fc.stateCount - 1));
        bits = fleetShift + fleetBits;
        extMask = (std::uint64_t{1} << vremShift) - 1;
    }

    [[nodiscard]] int ext(std::uint64_t k, int row) const {
        return static_cast<int>((k >> (3 * row)) & 7u);
    }
    [[nodiscard]] std::uint64_t setExt(std::uint64_t k, int row, int v) const {
        const int sh = 3 * row;
        return (k & ~(std::uint64_t{7} << sh)) | (static_cast<std::uint64_t>(v) << sh);
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
        std::size_t slot = mix(key) & mask_;
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
        if (ctx.col + L <= W && (ctx.allowH == nullptr || ctx.allowH[li])) {
            std::uint64_t k = lay.setExt(key, ctx.row, L - 1);
            k = lay.setVrem(k, 0);
            emit(lay.setFleet(k, nf));
        }
        if (ctx.row + L <= H && (ctx.allowV == nullptr || ctx.allowV[li])) {
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
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");
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
                                           mergeRange);

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
                    const std::size_t which = mix(dst) & (kRadix - 1);
                    bucket[which].push_back({dst, e.count});
                    ++edges;
                });
            }
            result.edges += edges;

            // Merge. Each bucket is independent, which is the whole point.
            // A layer too small to be worth a barrier is merged in place.
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
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return countConfigurationsBlocked(inst, c, threads);
}

}  // namespace mayflower
