// Exact uniform sampling by unranking.
//
// The unranker is a hard gate for everything downstream. Sequential rejection
// placement, placing the 5 then the 4 and so on, is not uniform: it over-weights
// configurations that leave room for the later ships, which would bias every
// statistic computed on top of it. This walks the lattice instead, so the map
// from rank to configuration is a bijection.
#include "mayflower/profile_dp.hpp"

#include "detail/v0_sweep.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mayflower {
namespace {

using detail::accepting;
using detail::CellCtx;
using detail::FleetCounter;
using detail::Kind;
using detail::makeCtx;
using detail::ProfileMap;
using detail::transitions;
using detail::Key;
using detail::packAux;

}  // namespace

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
