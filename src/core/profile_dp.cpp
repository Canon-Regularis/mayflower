#include "mayflower/profile_dp.hpp"

#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <utility>

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
        return mix(key.ext ^ (std::uint64_t{key.aux} * 0x9E3779B1u)) & mask_;
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

// Mixed-radix encoding of how many ships of each distinct length are started.
struct FleetCounter {
    std::vector<int> lengths;      // ascending distinct lengths
    std::vector<int> caps;
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
        fullIndex = stateCount - 1;

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

// Everything the transition function needs about one cell.
struct CellCtx {
    int  row = 0;
    int  col = 0;
    int  shift = 0;
    bool mustBeEmpty = false;
    bool mustBeOccupied = false;
    const std::uint8_t* allowH = nullptr;   // nLengths entries, or nullptr
    const std::uint8_t* allowV = nullptr;
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
        if (ctx.col + L <= W && (ctx.allowH == nullptr || ctx.allowH[li])) {
            emit(Key{key.ext | (static_cast<std::uint64_t>(L - 1) << ctx.shift),
                     packAux(0, nf)},
                 Kind::StartH, L);
        }
        if (ctx.row + L <= H && (ctx.allowV == nullptr || ctx.allowV[li])) {
            emit(Key{key.ext, packAux(L - 1, nf)}, Kind::StartV, L);
        }
    }
}

CellCtx makeCtx(const Instance& inst, const Constraints& c, const FleetCounter& fc,
                int row, int col) {
    const std::size_t cell = static_cast<std::size_t>(row * inst.width + col);
    const CellConstraint cc = c.cells[cell];
    CellCtx ctx;
    ctx.row = row;
    ctx.col = col;
    ctx.shift = 3 * row;
    ctx.mustBeEmpty = cc == CellConstraint::MustBeEmpty;
    ctx.mustBeOccupied = cc == CellConstraint::MustBeOccupied;
    if (c.gated()) {
        const std::size_t base = cell * fc.lengths.size();
        ctx.allowH = &c.allowH[base];
        ctx.allowV = &c.allowV[base];
    }
    return ctx;
}

bool accepting(const Key& key, const FleetCounter& fc) {
    return key.ext == 0 && auxVrem(key.aux) == 0 && auxFleet(key.aux) == fc.fullIndex;
}

}  // namespace

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

CountResult countConfigurations(const Instance& inst, const Constraints& constraints) {
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);

    ProfileMap cur(1024), next(1024);
    cur.add(Key{0, packAux(0, 0)}, 1);

    CountResult result;
    for (int col = 0; col < W; ++col) {
        for (int row = 0; row < H; ++row) {
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            result.peakStates = std::max(result.peakStates, cur.size());
            result.stateVisits += cur.size();

            next.clear();
            std::uint64_t edges = 0;
            cur.forEach([&](const Key& key, std::uint64_t count) {
                transitions(key, ctx, fc, W, H, [&](const Key& dst, Kind, int) {
                    next.add(dst, count);
                    ++edges;
                });
            });
            result.edges += edges;
            std::swap(cur, next);
        }
    }

    std::uint64_t total = 0;
    cur.forEach([&](const Key& key, std::uint64_t count) {
        if (accepting(key, fc)) total += count;
    });
    result.count = total;
    return result;
}

CountResult countConfigurations(const Instance& inst,
                                const std::vector<CellConstraint>& cells) {
    Constraints c;
    c.cells = cells;
    return countConfigurations(inst, c);
}

CountResult countConfigurations(const Instance& inst) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return countConfigurations(inst, c);
}

std::uint64_t occupancyCount(const Instance& inst, int row, int col) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    c.cells[static_cast<std::size_t>(row * inst.width + col)] = CellConstraint::MustBeOccupied;
    return countConfigurations(inst, c).count;
}

// ---------------------------------------------------------------------------
// Forward-backward marginals.
//
// Every configuration either occupies a cell or leaves it empty, and exactly one
// transition per layer carries it. So for cell t
//
//     occupancy(t) = total - flow through the "leave empty" transition at t
//
// and the empty transition is the identity on the state, which makes the scan a
// single walk over the layer computing F[s] * B[s].
//
// F is stored only at the W+1 column boundaries and replayed inside a column
// while B walks backward through it, which keeps the working set to one column
// instead of the whole lattice.
// ---------------------------------------------------------------------------

std::size_t placementSlots(const Instance& inst) {
    return static_cast<std::size_t>(inst.cellCount()) * 2 * inst.distinctLengths().size();
}

std::size_t placementIndex(const Instance& inst, int row, int col, int lengthIndex,
                           bool horizontal) {
    const std::size_t n = inst.distinctLengths().size();
    return (static_cast<std::size_t>(row * inst.width + col) * 2 + (horizontal ? 0u : 1u)) * n +
           static_cast<std::size_t>(lengthIndex);
}

LatticeFlows analyse(const Instance& inst, const Constraints& constraints) {
    inst.validate();
    if (constraints.cells.size() != static_cast<std::size_t>(inst.cellCount()))
        throw std::invalid_argument("constraint vector size must equal cellCount()");

    const int W = inst.width, H = inst.height;
    const FleetCounter fc(inst);
    const std::size_t nLengths = fc.lengths.size();

    using Layer = std::vector<std::pair<Key, std::uint64_t>>;

    LatticeFlows out;
    out.occupancy.assign(static_cast<std::size_t>(inst.cellCount()), 0);
    out.placement.assign(placementSlots(inst), 0);

    // Forward sweep, snapshotting column boundaries.
    std::vector<Layer> boundary(static_cast<std::size_t>(W) + 1);
    {
        ProfileMap cur(1024), next(1024);
        cur.add(Key{0, packAux(0, 0)}, 1);
        boundary[0] = cur.snapshot();
        for (int col = 0; col < W; ++col) {
            for (int row = 0; row < H; ++row) {
                const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
                next.clear();
                cur.forEach([&](const Key& key, std::uint64_t count) {
                    transitions(key, ctx, fc, W, H,
                                [&](const Key& dst, Kind, int) { next.add(dst, count); });
                });
                std::swap(cur, next);
            }
            boundary[static_cast<std::size_t>(col) + 1] = cur.snapshot();
        }
        cur.forEach([&](const Key& key, std::uint64_t count) {
            if (accepting(key, fc)) out.total += count;
        });
    }
    if (out.total == 0) return out;

    // Backward sweep, one column at a time.
    ProfileMap bNext(1024), bCur(1024), replayCur(1024), replayNext(1024);
    for (const auto& e : boundary[static_cast<std::size_t>(W)])
        if (accepting(e.first, fc)) bNext.add(e.first, 1);

    std::vector<Layer> fLayers(static_cast<std::size_t>(H));
    std::vector<int> lengthSlot(9, -1);
    for (std::size_t li = 0; li < nLengths; ++li)
        lengthSlot[static_cast<std::size_t>(fc.lengths[li])] = static_cast<int>(li);

    for (int col = W - 1; col >= 0; --col) {
        replayCur.load(boundary[static_cast<std::size_t>(col)]);
        for (int row = 0; row < H; ++row) {
            fLayers[static_cast<std::size_t>(row)] = replayCur.snapshot();
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            replayNext.clear();
            replayCur.forEach([&](const Key& key, std::uint64_t count) {
                transitions(key, ctx, fc, W, H,
                            [&](const Key& dst, Kind, int) { replayNext.add(dst, count); });
            });
            std::swap(replayCur, replayNext);
        }

        for (int row = H - 1; row >= 0; --row) {
            const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
            const Layer& F = fLayers[static_cast<std::size_t>(row)];
            const std::size_t cell = static_cast<std::size_t>(row * W + col);

            std::uint64_t emptyFlow = 0;
            bCur.clear();
            for (const auto& e : F) {
                std::uint64_t completions = 0;
                transitions(e.first, ctx, fc, W, H,
                            [&](const Key& dst, Kind kind, int len) {
                    const std::uint64_t b = bNext.get(dst);
                    completions += b;
                    if (b == 0) return;
                    if (kind == Kind::Empty) {
                        emptyFlow += e.second * b;
                    } else if (kind == Kind::StartH || kind == Kind::StartV) {
                        const int li = lengthSlot[static_cast<std::size_t>(len)];
                        out.placement[placementIndex(inst, row, col, li,
                                                     kind == Kind::StartH)] += e.second * b;
                    }
                });
                if (completions) bCur.add(e.first, completions);
            }
            out.occupancy[cell] = out.total - emptyFlow;
            std::swap(bCur, bNext);
        }
    }
    return out;
}

std::vector<std::uint64_t> occupancyMap(const Instance& inst,
                                        const Constraints& constraints,
                                        std::uint64_t& total) {
    LatticeFlows f = analyse(inst, constraints);
    total = f.total;
    return f.occupancy;
}

std::vector<std::uint64_t> occupancyMap(const Instance& inst, std::uint64_t& total) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    return occupancyMap(inst, c, total);
}

double OutcomeDistribution::hitProbability() const {
    const std::uint64_t t = total();
    if (t == 0) return 0.0;
    return static_cast<double>(t - miss) / static_cast<double>(t);
}

double OutcomeDistribution::informationBits() const {
    const std::uint64_t t = total();
    if (t == 0) return 0.0;
    double h = 0.0;
    const auto term = [&](std::uint64_t n) {
        if (n == 0 || n == t) return;
        const double q = static_cast<double>(n) / static_cast<double>(t);
        h -= q * std::log2(q);
    };
    term(miss);
    term(hit);
    for (std::uint64_t v : sunk) term(v);
    return h;
}

std::vector<OutcomeDistribution> outcomeDistribution(const Instance& inst,
                                                     const History& history,
                                                     std::uint64_t& total) {
    const Constraints constraints = constraintsFrom(inst, history);
    const LatticeFlows flows = analyse(inst, constraints);
    total = flows.total;

    const int W = inst.width, H = inst.height;
    const std::vector<int> lengths = inst.distinctLengths();
    std::vector<OutcomeDistribution> out(static_cast<std::size_t>(inst.cellCount()));
    if (flows.total == 0) return out;

    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            const int cell = row * W + col;
            OutcomeDistribution& d = out[static_cast<std::size_t>(cell)];
            if (history.shot(cell)) continue;
            d.shootable = true;
            d.miss = flows.total - flows.occupancy[static_cast<std::size_t>(cell)];

            for (std::size_t li = 0; li < lengths.size(); ++li) {
                const int L = lengths[li];
                for (int k = 0; k < L; ++k) {   // horizontal placements covering the cell
                    const int c0 = col - k;
                    if (c0 < 0 || c0 + L > W) continue;
                    const std::uint64_t f =
                        flows.placement[placementIndex(inst, row, c0, static_cast<int>(li), true)];
                    if (f == 0) continue;
                    bool sinks = true;
                    for (int t = 0; t < L && sinks; ++t) {
                        const int other = row * W + c0 + t;
                        if (other != cell && !history.shot(other)) sinks = false;
                    }
                    if (sinks) d.sunk[static_cast<std::size_t>(L)] += f;
                    else       d.hit += f;
                }
                for (int k = 0; k < L; ++k) {   // vertical placements covering the cell
                    const int r0 = row - k;
                    if (r0 < 0 || r0 + L > H) continue;
                    const std::uint64_t f =
                        flows.placement[placementIndex(inst, r0, col, static_cast<int>(li), false)];
                    if (f == 0) continue;
                    bool sinks = true;
                    for (int t = 0; t < L && sinks; ++t) {
                        const int other = (r0 + t) * W + col;
                        if (other != cell && !history.shot(other)) sinks = false;
                    }
                    if (sinks) d.sunk[static_cast<std::size_t>(L)] += f;
                    else       d.hit += f;
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Sampler
// ---------------------------------------------------------------------------

struct Sampler::Impl {
    Instance     inst;
    Constraints  constraints;
    FleetCounter fc;
    int W = 0, H = 0, cells = 0;
    std::uint64_t total = 0;
    std::vector<ProfileMap> b;   // backward completion counts, one map per layer
    std::size_t entries = 0;

    Impl(const Instance& i, const Constraints& c)
        : inst(i), constraints(c), fc(i), W(i.width), H(i.height), cells(i.cellCount()) {
        build();
    }

    void build() {
        using Layer = std::vector<std::pair<Key, std::uint64_t>>;

        // Forward sweep, snapshotting column boundaries.
        std::vector<Layer> boundary(static_cast<std::size_t>(W) + 1);
        ProfileMap cur(1024), next(1024);
        cur.add(Key{0, packAux(0, 0)}, 1);
        boundary[0] = cur.snapshot();
        for (int col = 0; col < W; ++col) {
            for (int row = 0; row < H; ++row) {
                const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
                next.clear();
                cur.forEach([&](const Key& key, std::uint64_t count) {
                    transitions(key, ctx, fc, W, H,
                                [&](const Key& dst, Kind, int) { next.add(dst, count); });
                });
                std::swap(cur, next);
            }
            boundary[static_cast<std::size_t>(col) + 1] = cur.snapshot();
        }
        total = 0;
        cur.forEach([&](const Key& key, std::uint64_t count) {
            if (accepting(key, fc)) total += count;
        });

        // Backward sweep, filling every layer. F is replayed one column at a
        // time from its left boundary, so only one column of forward layers is
        // held at once.
        b.assign(static_cast<std::size_t>(cells) + 1, ProfileMap{});
        for (const auto& e : boundary[static_cast<std::size_t>(W)])
            if (accepting(e.first, fc)) b[static_cast<std::size_t>(cells)].add(e.first, 1);

        ProfileMap replayCur(1024), replayNext(1024);
        std::vector<Layer> fLayers(static_cast<std::size_t>(H));
        for (int col = W - 1; col >= 0; --col) {
            replayCur.load(boundary[static_cast<std::size_t>(col)]);
            for (int row = 0; row < H; ++row) {
                fLayers[static_cast<std::size_t>(row)] = replayCur.snapshot();
                const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
                replayNext.clear();
                replayCur.forEach([&](const Key& key, std::uint64_t count) {
                    transitions(key, ctx, fc, W, H,
                                [&](const Key& dst, Kind, int) { replayNext.add(dst, count); });
                });
                std::swap(replayCur, replayNext);
            }
            for (int row = H - 1; row >= 0; --row) {
                const CellCtx ctx = makeCtx(inst, constraints, fc, row, col);
                const std::size_t layer = static_cast<std::size_t>(col * H + row);
                const Layer& F = fLayers[static_cast<std::size_t>(row)];
                for (const auto& e : F) {
                    std::uint64_t completions = 0;
                    transitions(e.first, ctx, fc, W, H,
                                [&](const Key& dst, Kind, int) {
                        completions += b[layer + 1].get(dst);
                    });
                    if (completions) b[layer].add(e.first, completions);
                }
            }
        }
        entries = 0;
        for (const ProfileMap& m : b) entries += m.size();
    }
};

Sampler::Sampler(const Instance& inst, const Constraints& constraints)
    : impl_(std::make_unique<Impl>(inst, constraints)) {}

Sampler::Sampler(const Instance& inst) : impl_(nullptr) {
    Constraints c;
    c.cells.assign(static_cast<std::size_t>(inst.cellCount()), CellConstraint::Free);
    impl_ = std::make_unique<Impl>(inst, c);
}

Sampler::~Sampler() = default;
Sampler::Sampler(Sampler&&) noexcept = default;
Sampler& Sampler::operator=(Sampler&&) noexcept = default;

std::uint64_t Sampler::total() const { return impl_->total; }
std::size_t Sampler::storedEntries() const { return impl_->entries; }

std::vector<ShipPlacement> Sampler::unrank(std::uint64_t rank) const {
    const Impl& im = *impl_;
    if (rank >= im.total) throw std::out_of_range("rank must lie in [0, total())");

    struct Cand { Key dst; Kind kind; int len; std::uint64_t weight; };
    Cand cand[24];

    Key state{0, packAux(0, 0)};
    std::vector<ShipPlacement> out;
    out.reserve(im.inst.fleet.size());

    for (int col = 0; col < im.W; ++col) {
        for (int row = 0; row < im.H; ++row) {
            const CellCtx ctx = makeCtx(im.inst, im.constraints, im.fc, row, col);
            const std::size_t layer = static_cast<std::size_t>(col * im.H + row);

            int n = 0;
            transitions(state, ctx, im.fc, im.W, im.H,
                        [&](const Key& dst, Kind kind, int len) {
                if (n >= 24) throw std::logic_error("transition fan-out exceeded");
                const std::uint64_t w = im.b[layer + 1].get(dst);
                if (w) cand[n++] = {dst, kind, len, w};
            });

            std::uint64_t acc = 0;
            int chosen = -1;
            for (int i = 0; i < n; ++i) {
                if (rank < acc + cand[i].weight) { chosen = i; break; }
                acc += cand[i].weight;
            }
            if (chosen < 0) throw std::logic_error("rank walk left the lattice");
            rank -= acc;

            if (cand[chosen].kind == Kind::StartH)
                out.push_back(ShipPlacement{row, col, cand[chosen].len, true});
            else if (cand[chosen].kind == Kind::StartV)
                out.push_back(ShipPlacement{row, col, cand[chosen].len, false});

            state = cand[chosen].dst;
        }
    }
    return out;
}

}  // namespace mayflower
