#include "mayflower/weighted.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
constexpr int auxVrem(std::uint32_t aux) { return static_cast<int>(aux & 7u); }
constexpr int auxFleet(std::uint32_t aux) { return static_cast<int>(aux >> 3); }
constexpr int extDigit(std::uint64_t ext, int row) {
    return static_cast<int>((ext >> (3 * row)) & 7u);
}

// Mirrors the counter in profile_dp.cpp.
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

// Same shape as ProfileMap, carrying a double instead of a count.
class WeightMap {
public:
    explicit WeightMap(std::size_t capacityPow2 = 1024) { reserve(capacityPow2); }

    void reserve(std::size_t capacityPow2) {
        capacity_ = 1;
        while (capacity_ < capacityPow2) capacity_ <<= 1;
        mask_ = capacity_ - 1;
        keys_.assign(capacity_, Key{});
        vals_.assign(capacity_, 0.0);
        used_.assign(capacity_, false);
        dense_.clear();
        dense_.reserve(capacityPow2);
    }

    void clear() {
        for (std::size_t slot : dense_) used_[slot] = false;
        dense_.clear();
    }

    [[nodiscard]] std::size_t size() const { return dense_.size(); }

    void add(const Key& key, double value) {
        std::size_t slot = probe(key);
        while (true) {
            if (!used_[slot]) {
                if (dense_.size() * 10 >= capacity_ * 7) {
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

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (std::size_t slot : dense_) fn(keys_[slot], vals_[slot]);
    }

    [[nodiscard]] double get(const Key& key) const {
        std::size_t slot = probe(key);
        while (used_[slot]) {
            if (keys_[slot] == key) return vals_[slot];
            slot = (slot + 1) & mask_;
        }
        return 0.0;
    }

    template <typename Fn>
    void forEachSlot(Fn&& fn) const {
        for (std::size_t slot : dense_) fn(keys_[slot], vals_[slot]);
    }

    // ldexp edits the exponent and leaves the mantissa alone, which is exact
    // while the result stays normal and not once it does not. The scale is one
    // power of two for the whole layer, so bringing the maximum back into range
    // pushes everything a factor of 1e308 behind it out of range, and those
    // values reach zero. Returns whether any did, because a layer that lost a
    // value has lost the configurations that ran through it.
    [[nodiscard]] bool scaleByPowerOfTwo(int exponent) {
        bool lost = false;
        for (std::size_t slot : dense_) {
            const double before = vals_[slot];
            const double after = std::ldexp(before, exponent);
            if (after == 0.0 && before != 0.0) lost = true;
            vals_[slot] = after;
        }
        return lost;
    }

    [[nodiscard]] double maxValue() const {
        double m = 0;
        for (std::size_t slot : dense_) m = std::max(m, vals_[slot]);
        return m;
    }

    [[nodiscard]] double sum() const {
        double s = 0;
        for (std::size_t slot : dense_) s += vals_[slot];
        return s;
    }

private:
    [[nodiscard]] std::size_t probe(const Key& key) const {
        return mix(key.ext ^ (std::uint64_t{key.aux} * 0x9E3779B1u)) & mask_;
    }

    void grow() {
        std::vector<std::pair<Key, double>> old;
        old.reserve(dense_.size());
        for (std::size_t slot : dense_) old.emplace_back(keys_[slot], vals_[slot]);
        reserve(capacity_ * 2);
        for (const auto& e : old) add(e.first, e.second);
    }

    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::vector<Key> keys_;
    std::vector<double> vals_;
    std::vector<bool> used_;
    std::vector<std::size_t> dense_;
};

std::vector<std::pair<Key, double>> snapshotOf(const WeightMap& m) {
    std::vector<std::pair<Key, double>> out;
    m.forEach([&](const Key& k, double v) { out.emplace_back(k, v); });
    return out;
}

void loadInto(WeightMap& m, const std::vector<std::pair<Key, double>>& entries) {
    m.clear();
    for (const auto& e : entries) m.add(e.first, e.second);
}

struct CellCtx {
    int row = 0;
    int col = 0;
    int shift = 0;
    bool mustBeEmpty = false;
    bool mustBeOccupied = false;
    const std::uint8_t* allowH = nullptr;
    const std::uint8_t* allowV = nullptr;
    // Weight lookups, already resolved for this cell. A null pointer means 1.
    double occupied = 1.0;
    double empty = 1.0;
    const double* startH = nullptr;
    const double* startV = nullptr;
};

CellCtx makeCtx(const Instance& inst, const Constraints& c, const Weights& w,
                const FleetCounter& fc, int row, int col) {
    const std::size_t cell = static_cast<std::size_t>(row * inst.width + col);
    CellCtx ctx;
    ctx.row = row;
    ctx.col = col;
    ctx.shift = 3 * row;
    ctx.mustBeEmpty = c.cells[cell] == CellConstraint::MustBeEmpty;
    ctx.mustBeOccupied = c.cells[cell] == CellConstraint::MustBeOccupied;
    if (c.gated()) {
        const std::size_t base = cell * fc.lengths.size();
        ctx.allowH = &c.allowH[base];
        ctx.allowV = &c.allowV[base];
    }
    if (!w.occupied.empty()) ctx.occupied = w.occupied[cell];
    if (!w.empty.empty()) ctx.empty = w.empty[cell];
    const std::size_t base = cell * fc.lengths.size();
    if (!w.startH.empty()) ctx.startH = &w.startH[base];
    if (!w.startV.empty()) ctx.startV = &w.startV[base];
    return ctx;
}

// The transition relation, mirroring profile_dp.cpp with a weight on each edge.
template <typename Emit>
inline void transitions(const Key& key, const CellCtx& ctx, const FleetCounter& fc,
                        int W, int H, Emit&& emit) {
    const int vrem = auxVrem(key.aux);
    const int fleet = auxFleet(key.aux);
    const int d = extDigit(key.ext, ctx.row);

    if (d > 0) {
        if (vrem > 0 || ctx.mustBeEmpty) return;
        emit(Key{key.ext - (std::uint64_t{1} << ctx.shift), key.aux}, ctx.occupied, false);
        return;
    }
    if (vrem > 0) {
        if (ctx.mustBeEmpty) return;
        emit(Key{key.ext, packAux(vrem - 1, fleet)}, ctx.occupied, false);
        return;
    }
    if (!ctx.mustBeOccupied) emit(key, ctx.empty, true);
    if (ctx.mustBeEmpty) return;

    const std::size_t nLengths = fc.lengths.size();
    for (std::size_t li = 0; li < nLengths; ++li) {
        const int L = fc.lengths[li];
        const int nf = fc.afterStarting(fleet, li);
        if (nf < 0) continue;
        if (ctx.col + L <= W && (ctx.allowH == nullptr || ctx.allowH[li])) {
            const double w = ctx.startH ? ctx.occupied * ctx.startH[li] : ctx.occupied;
            emit(Key{key.ext | (static_cast<std::uint64_t>(L - 1) << ctx.shift),
                     packAux(0, nf)},
                 w, false);
        }
        // A length-1 ship has one placement, not two, so only the horizontal
        // branch emits it. Real fleets start at 2 and never reach this.
        if (L > 1 && ctx.row + L <= H && (ctx.allowV == nullptr || ctx.allowV[li])) {
            const double w = ctx.startV ? ctx.occupied * ctx.startV[li] : ctx.occupied;
            emit(Key{key.ext, packAux(L - 1, nf)}, w, false);
        }
    }
}

bool accepting(const Key& key, const FleetCounter& fc) {
    return key.ext == 0 && auxVrem(key.aux) == 0 && auxFleet(key.aux) == fc.fullIndex;
}

constexpr double kRescaleHigh = 1e250;
constexpr double kRescaleLow = 1e-250;

// Rescaling divides by a power of two rather than by the layer maximum, so it
// only edits exponents and leaves every mantissa alone. The scale is then an
// exact integer and rescaling costs no precision at all, which matters because
// a run that rescales must stay as trustworthy as one that does not.
struct Scale {
    long long exponent = 0;

    [[nodiscard]] double apply(double raw) const {
        return std::ldexp(raw, static_cast<int>(
            std::max<long long>(std::min<long long>(exponent, 40000), -40000)));
    }
    [[nodiscard]] double log(double raw) const {
        return std::log(raw) + static_cast<double>(exponent) * 0.69314718055994530942;
    }
};

}  // namespace

// ---------------------------------------------------------------------------

Weights Weights::noisyChannel(const Instance& inst, const std::vector<int>& answers,
                              double eps) {
    if (answers.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("answers must have one entry per cell");
    if (!(eps > 0.0 && eps <= 0.5))
        throw std::invalid_argument("eps must lie in (0, 0.5]; eps = 0 is a hard constraint");

    Weights w;
    w.occupied.assign(answers.size(), 1.0);
    w.empty.assign(answers.size(), 1.0);
    for (std::size_t i = 0; i < answers.size(); ++i) {
        if (answers[i] < 0) continue;
        const bool saidOccupied = answers[i] != 0;
        w.occupied[i] = saidOccupied ? (1.0 - eps) : eps;
        w.empty[i] = saidOccupied ? eps : (1.0 - eps);
    }
    return w;
}

Weights Weights::fromLogPlacementScores(const Instance& inst,
                                        const std::vector<double>& logH,
                                        const std::vector<double>& logV) {
    const std::size_t slots = static_cast<std::size_t>(inst.cellCount()) *
                              inst.distinctLengths().size();
    if (logH.size() != slots || logV.size() != slots)
        throw std::invalid_argument("score vectors must have cellCount * nLengths entries");

    Weights w;
    w.startH.resize(slots);
    w.startV.resize(slots);
    for (std::size_t i = 0; i < slots; ++i) {
        w.startH[i] = std::exp(logH[i]);
        w.startV[i] = std::exp(logV[i]);
    }
    return w;
}

WeightedResult weightedCount(const Instance& inst, const Constraints& constraints,
                             const Weights& weights) {
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);
    const std::size_t slots =
        static_cast<std::size_t>(inst.cellCount()) * fc.lengths.size();
    if ((!weights.startH.empty() && weights.startH.size() != slots) ||
        (!weights.startV.empty() && weights.startV.size() != slots))
        throw std::invalid_argument("placement weights must have cellCount * nLengths entries");
    if ((!weights.occupied.empty() &&
         weights.occupied.size() != static_cast<std::size_t>(inst.cellCount())) ||
        (!weights.empty.empty() &&
         weights.empty.size() != static_cast<std::size_t>(inst.cellCount())))
        throw std::invalid_argument("cell weights must have one entry per cell");

    WeightMap cur(1024), next(1024);
    cur.add(Key{0, packAux(0, 0)}, 1.0);

    WeightedResult result;
    Scale scale;

    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const CellCtx ctx = makeCtx(inst, constraints, weights, fc, row, col);
            result.peakStates = std::max(result.peakStates, cur.size());
            result.maxLayerSum = std::max(result.maxLayerSum, cur.sum());

            next.clear();
            std::uint64_t edges = 0;
            cur.forEach([&](const Key& key, double value) {
                transitions(key, ctx, fc, W, H,
                            [&](const Key& dst, double w, bool) {
                    const double v = value * w;
                    // Two non-zero factors giving zero is a configuration lost,
                    // and it is the only way one can be lost here.
                    if (v == 0.0 && value != 0.0 && w != 0.0) result.underflowed = true;
                    next.add(dst, v);
                    ++edges;
                });
            });
            result.edges += edges;
            std::swap(cur, next);

            // Rescale only when a layer threatens the exponent range. The
            // unweighted case never triggers it, so the bridge to the integer
            // count is bit-exact for the obvious reason as well as this one.
            const double m = cur.maxValue();
            if (m > kRescaleHigh || (m > 0 && m < kRescaleLow)) {
                int ex = 0;
                std::frexp(m, &ex);              // m = mantissa * 2^ex, mantissa in [0.5,1)
                if (cur.scaleByPowerOfTwo(-ex)) result.underflowed = true;
                scale.exponent += ex;
                result.rescaled = true;
            }
        }
    }

    // The last cell's output layer, which the loop above never sampled: it
    // measures the layer entering each cell, so the layer the final cell
    // produces is the one total is read out of and the one the certificate
    // would otherwise skip. It matters only when a cell can emit more than one
    // edge per state at the end of the sweep, which needs a length-1 ship, and
    // the certificate has to hold for every instance the library accepts.
    result.peakStates = std::max(result.peakStates, cur.size());
    result.maxLayerSum = std::max(result.maxLayerSum, cur.sum());

    double total = 0;
    cur.forEach([&](const Key& key, double value) {
        if (accepting(key, fc)) total += value;
    });

    // Reconstructing through ldexp rounds once and saturates correctly, where
    // total * exp(logScale) would overflow the intermediate whenever logScale
    // passed 709 even if the product itself were representable.
    // Self-certifying exactness. Integers below 2^53 add exactly in a double, so
    // an unweighted run is bit-exact provided no layer reached that far and no
    // rescale ran. Anything else is floating point and says so.
    constexpr double kExactLimit = 9007199254740992.0;   // 2^53
    result.exact = weights.trivial() && !result.rescaled && !result.underflowed &&
                   result.maxLayerSum < kExactLimit;

    result.total = total > 0 ? scale.apply(total) : 0.0;
    result.logTotal =
        total > 0 ? scale.log(total) : -std::numeric_limits<double>::infinity();
    return result;
}

WeightedResult weightedCount(const Instance& inst, const Weights& weights) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return weightedCount(inst, c, weights);
}

double weightedMarginal(const Instance& inst, const Constraints& constraints,
                        const Weights& weights, int cell) {
    // A cell the record has already settled is not a question. Forcing it the
    // other way would count a disjoint set of boards and divide it by this one,
    // which is not a conditional probability and is not bounded by 1.
    const CellConstraint fixed = constraints.cells[static_cast<std::size_t>(cell)];
    if (fixed == CellConstraint::MustBeEmpty) return 0.0;

    const WeightedResult all = weightedCount(inst, constraints, weights);
    if (all.logTotal == -std::numeric_limits<double>::infinity()) return 0.0;
    if (fixed == CellConstraint::MustBeOccupied) return 1.0;

    Constraints forced = constraints;
    forced.cells[static_cast<std::size_t>(cell)] = CellConstraint::MustBeOccupied;
    const WeightedResult hit = weightedCount(inst, forced, weights);
    if (hit.logTotal == -std::numeric_limits<double>::infinity()) return 0.0;

    // Through logs, so an overflowing total does not poison the ratio.
    return std::exp(hit.logTotal - all.logTotal);
}

std::vector<double> weightedMarginalsByRecount(const Instance& inst,
                                               const Constraints& constraints,
                                               const Weights& weights) {
    const WeightedResult all = weightedCount(inst, constraints, weights);
    std::vector<double> out(static_cast<std::size_t>(inst.cellCount()), 0.0);
    if (all.logTotal == -std::numeric_limits<double>::infinity()) return out;

    for (int cell = 0; cell < inst.cellCount(); ++cell) {
        // Same rule as weightedMarginal: a settled cell is read off the record
        // rather than recounted against it.
        const CellConstraint fixed = constraints.cells[static_cast<std::size_t>(cell)];
        if (fixed == CellConstraint::MustBeEmpty) continue;              // stays 0
        if (fixed == CellConstraint::MustBeOccupied) {
            out[static_cast<std::size_t>(cell)] = 1.0;
            continue;
        }
        Constraints forced = constraints;
        forced.cells[static_cast<std::size_t>(cell)] = CellConstraint::MustBeOccupied;
        const WeightedResult hit = weightedCount(inst, forced, weights);
        out[static_cast<std::size_t>(cell)] =
            hit.logTotal == -std::numeric_limits<double>::infinity()
                ? 0.0
                : std::exp(hit.logTotal - all.logTotal);
    }
    return out;
}

constexpr const char* kUnderflowMessage =
    "weightedMarginals: the weights span more than a double holds, so a product of "
    "two representable factors underflowed and configurations were dropped. Use "
    "weightedMarginalsByRecount, which divides two equally scaled counts.";

std::vector<double> weightedMarginals(const Instance& inst, const Constraints& constraints,
                                      const Weights& weights) {
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);
    std::vector<double> out(static_cast<std::size_t>(inst.cellCount()), 0.0);

    using Layer = std::vector<std::pair<Key, double>>;

    // Forward, keeping only the column boundaries. Everything between them is
    // replayed later, which is what keeps this inside memory.
    //
    // Rescaling is deliberately absent here. The backward pass has to combine f
    // and b from the same layer, and a per-layer scale would have to be undone
    // consistently on both sides; the marginal is a ratio, so any global scale
    // cancels, but a per-layer one does not. Callers whose weights leave a double
    // should use the recount path, which divides two equally scaled counts and so
    // survives weights this cannot hold.
    //
    // Overflow is loud. Underflow is not, and it is the direction this fails in:
    // f and b are each representable while the product f * b is not, and the
    // marginals come back inside [0, 1] with nothing wrong on the face of them.
    // With a uniform weight of 1e-13 on a 5x5 {3,2,2} board, where the weight
    // cancels in every ratio and the answer must not move at all, they move by
    // 0.047; at 1e-14 they are all zero while the recount is still right to 2e-14.
    // So the multiply is watched, and a sweep that has lost a configuration says
    // so rather than returning the part it kept.
    std::vector<Layer> boundary(static_cast<std::size_t>(W) + 1);
    double total = 0;
    bool underflowed = false;
    const auto note = [&](double a, double b, double product) {
        if (product == 0.0 && a != 0.0 && b != 0.0) underflowed = true;
    };
    {
        WeightMap cur(1024), next(1024);
        cur.add(Key{0, packAux(0, 0)}, 1.0);
        boundary[0] = snapshotOf(cur);
        for (int col = 0; col < W; ++col) {
            for (int row = 0; row < H; ++row) {
                const CellCtx ctx = makeCtx(inst, constraints, weights, fc, row, col);
                next.clear();
                cur.forEach([&](const Key& key, double value) {
                    transitions(key, ctx, fc, W, H,
                                [&](const Key& dst, double w, bool) {
                        const double v = value * w;
                        note(value, w, v);
                        next.add(dst, v);
                    });
                });
                std::swap(cur, next);
            }
            boundary[static_cast<std::size_t>(col) + 1] = snapshotOf(cur);
        }
        cur.forEach([&](const Key& key, double value) {
            if (accepting(key, fc)) total += value;
        });
    }
    if (!(total > 0)) {
        // Zero here means either that no configuration survives the record or
        // that every one of them underflowed away, and those want opposite
        // answers from the caller.
        if (underflowed) throw std::runtime_error(kUnderflowMessage);
        return out;
    }

    // Backward, one column at a time, replaying the forward layers inside it.
    WeightMap bNext(1024), bCur(1024), replayCur(1024), replayNext(1024);
    for (const auto& e : boundary[static_cast<std::size_t>(W)])
        if (accepting(e.first, fc)) bNext.add(e.first, 1.0);

    std::vector<Layer> fLayers(static_cast<std::size_t>(H));
    for (int col = W - 1; col >= 0; --col) {
        loadInto(replayCur, boundary[static_cast<std::size_t>(col)]);
        for (int row = 0; row < H; ++row) {
            fLayers[static_cast<std::size_t>(row)] = snapshotOf(replayCur);
            const CellCtx ctx = makeCtx(inst, constraints, weights, fc, row, col);
            replayNext.clear();
            replayCur.forEach([&](const Key& key, double value) {
                transitions(key, ctx, fc, W, H,
                            [&](const Key& dst, double w, bool) {
                    const double v = value * w;
                    note(value, w, v);
                    replayNext.add(dst, v);
                });
            });
            std::swap(replayCur, replayNext);
        }

        for (int row = H - 1; row >= 0; --row) {
            const CellCtx ctx = makeCtx(inst, constraints, weights, fc, row, col);
            const Layer& F = fLayers[static_cast<std::size_t>(row)];
            const std::size_t cell = static_cast<std::size_t>(row * W + col);

            double emptyFlow = 0;
            bCur.clear();
            for (const auto& e : F) {
                double completions = 0;
                transitions(e.first, ctx, fc, W, H,
                            [&](const Key& dst, double w, bool isEmpty) {
                    const double b = bNext.get(dst);
                    if (b == 0) return;
                    const double wb = w * b;
                    note(w, b, wb);
                    completions += wb;
                    if (isEmpty) {
                        const double flow = e.second * wb;
                        note(e.second, wb, flow);
                        emptyFlow += flow;
                    }
                });
                if (completions != 0) bCur.add(e.first, completions);
            }
            // Everything that did not leave the cell empty occupied it.
            //
            // A cell the constraints have already settled is written straight
            // in. Deriving it from this subtraction is correct in exact
            // arithmetic but leaves about one ulp of dust in floating point,
            // because total and emptyFlow are the same sum accumulated in
            // different orders. A cell known to be empty has posterior zero, not
            // 1.6e-16, and saying so costs nothing.
            const CellConstraint settled = constraints.cells[cell];
            if (settled == CellConstraint::MustBeEmpty) {
                out[cell] = 0.0;
            } else if (settled == CellConstraint::MustBeOccupied) {
                out[cell] = 1.0;
            } else {
                const double occupied = total - emptyFlow;
                out[cell] = occupied > 0 ? occupied / total : 0.0;
            }
            std::swap(bCur, bNext);
        }
    }
    if (underflowed) throw std::runtime_error(kUnderflowMessage);
    return out;
}

}  // namespace mayflower
