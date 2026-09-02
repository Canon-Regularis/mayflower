// Shot-selection policies.
//
// Two tiers, as the compute budget requires. The cheap tier works from bitboard
// placement counting and runs in microseconds, so it carries the large-N
// statistics. The exact tier works from the DP posterior and is far slower, so
// it carries the headline table and the bound comparison.
//
// Every policy is deterministic given (board, seed): ties break on the lowest
// cell index, and the only randomness is the seeded stream in reset().
#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "mayflower/game.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"

namespace mayflower {

namespace detail {

struct Stream {
    std::uint64_t s = 0;
    explicit Stream(std::uint64_t seed = 0) : s(seed) {}
    std::uint64_t next() {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Refused rather than divided by. Every policy ends in below(free.size()),
    // and a policy asked to choose with nothing free reached `% 0`. The harness
    // never does that, since it stops once the ship cells are gone, but
    // chooseShot is public and the failure was a crash rather than an error.
    int below(int n) {
        if (n <= 0) throw std::invalid_argument("below() needs a positive bound");
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }
};

// What a heuristic policy needs from the history.
//
// Sink attribution is a heuristic here. When a ship of length L is announced
// sunk at cell x, the cells it occupied are inferred greedily from the
// contiguous run of unresolved hits through x. That inference can be wrong when
// ships touch, which is one of the things the exact engine gets right and the
// cheap engine does not.
struct Situation {
    std::vector<char> shot;
    std::vector<char> miss;
    std::vector<char> resolved;     // hits attributed to a sunk ship
    std::vector<char> openHit;      // hits not yet attributed
    std::vector<int>  remainingFleet;
    int openHitCount = 0;

    Situation(const Instance& inst, const History& h) {
        const int W = inst.width, H = inst.height;
        const std::size_t n = static_cast<std::size_t>(inst.cellCount());
        shot.assign(n, 0);
        miss.assign(n, 0);
        resolved.assign(n, 0);
        openHit.assign(n, 0);
        remainingFleet = inst.fleet;

        for (int cell : h.sequence()) {
            const std::size_t c = static_cast<std::size_t>(cell);
            shot[c] = 1;
            switch (h.outcome(cell)) {
                case Outcome::Miss: miss[c] = 1; break;
                case Outcome::Hit:  openHit[c] = 1; break;
                case Outcome::Sunk: {
                    openHit[c] = 1;
                    const int L = h.sunkLength(cell);
                    auto it = std::find(remainingFleet.begin(), remainingFleet.end(), L);
                    if (it != remainingFleet.end()) remainingFleet.erase(it);
                    attribute(W, H, cell, L);
                    break;
                }
            }
        }
        for (std::size_t c = 0; c < n; ++c) if (openHit[c]) ++openHitCount;
    }

private:
    // Mark L cells around `cell` as belonging to the ship that just sank,
    // walking whichever axis carries more unresolved hits.
    void attribute(int W, int H, int cell, int L) {
        const int row = cell / W, col = cell % W;
        int bestAxis = 0, bestReach = -1;
        for (int axis = 0; axis < 2; ++axis) {
            int reach = 1;
            for (int dir = -1; dir <= 1; dir += 2) {
                for (int step = 1; step < L; ++step) {
                    const int r = axis == 0 ? row : row + dir * step;
                    const int c = axis == 0 ? col + dir * step : col;
                    if (r < 0 || r >= H || c < 0 || c >= W) break;
                    if (!openHit[static_cast<std::size_t>(r * W + c)]) break;
                    ++reach;
                }
            }
            if (reach > bestReach) { bestReach = reach; bestAxis = axis; }
        }

        std::vector<int> run{cell};
        for (int dir = -1; dir <= 1; dir += 2) {
            for (int step = 1; static_cast<int>(run.size()) < L; ++step) {
                const int r = bestAxis == 0 ? row : row + dir * step;
                const int c = bestAxis == 0 ? col + dir * step : col;
                if (r < 0 || r >= H || c < 0 || c >= W) break;
                const std::size_t idx = static_cast<std::size_t>(r * W + c);
                if (!openHit[idx]) break;
                run.push_back(static_cast<int>(idx));
            }
        }
        for (int c : run) {
            openHit[static_cast<std::size_t>(c)] = 0;
            resolved[static_cast<std::size_t>(c)] = 1;
        }
    }
};

inline int firstUnshot(const Situation& s) {
    for (std::size_t c = 0; c < s.shot.size(); ++c) if (!s.shot[c]) return static_cast<int>(c);
    return -1;
}

}  // namespace detail

// ---------------------------------------------------------------------------

// Uniform over unshot cells. The game ends on the last of the k ship cells, so
// the expected shot count has a closed form, k(N+1)/(k+1) over N cells, which
// makes it the harness self-test: 95.3889 on the standard instance.
class RandomPolicy : public Policy {
public:
    [[nodiscard]] const char* name() const override { return "random"; }
    void reset(const Instance&, std::uint64_t seed) override { rng_ = detail::Stream(seed); }
    [[nodiscard]] int chooseShot(const Instance& inst, const History& h) override {
        std::vector<int> free;
        free.reserve(static_cast<std::size_t>(inst.cellCount()));
        for (int c = 0; c < inst.cellCount(); ++c) if (!h.shot(c)) free.push_back(c);
        return free[static_cast<std::size_t>(rng_.below(static_cast<int>(free.size())))];
    }

private:
    detail::Stream rng_;
};

// Hunt on one parity class, then extend from unresolved hits. Any ship of length
// two or more covers at least one cell of each diagonal parity class, so hunting
// one class alone still finds every ship.
class ParityHuntTarget : public Policy {
public:
    [[nodiscard]] const char* name() const override { return "parity-hunt-target"; }
    void reset(const Instance&, std::uint64_t seed) override { rng_ = detail::Stream(seed); }

    [[nodiscard]] int chooseShot(const Instance& inst, const History& h) override {
        const detail::Situation s(inst, h);
        const int W = inst.width, H = inst.height;

        if (s.openHitCount > 0) {
            // Extend a line of two or more collinear open hits first.
            for (int cell = 0; cell < inst.cellCount(); ++cell) {
                if (!s.openHit[static_cast<std::size_t>(cell)]) continue;
                const int row = cell / W, col = cell % W;
                for (int axis = 0; axis < 2; ++axis) {
                    for (int dir = -1; dir <= 1; dir += 2) {
                        const int nr = axis == 0 ? row : row + dir;
                        const int nc = axis == 0 ? col + dir : col;
                        if (nr < 0 || nr >= H || nc < 0 || nc >= W) continue;
                        const int neighbour = nr * W + nc;
                        if (!s.openHit[static_cast<std::size_t>(neighbour)]) continue;
                        // Collinear pair found; push out from both ends.
                        for (int end = -1; end <= 1; end += 2) {
                            for (int step = 1; step <= inst.maxShipLength(); ++step) {
                                const int rr = axis == 0 ? row : row + end * step;
                                const int cc = axis == 0 ? col + end * step : col;
                                if (rr < 0 || rr >= H || cc < 0 || cc >= W) break;
                                const std::size_t idx = static_cast<std::size_t>(rr * W + cc);
                                if (s.openHit[idx]) continue;
                                if (s.shot[idx]) break;
                                return static_cast<int>(idx);
                            }
                        }
                    }
                }
            }
            // Otherwise any unshot orthogonal neighbour of an open hit.
            for (int cell = 0; cell < inst.cellCount(); ++cell) {
                if (!s.openHit[static_cast<std::size_t>(cell)]) continue;
                const int row = cell / W, col = cell % W;
                const int dr[4] = {-1, 1, 0, 0};
                const int dc[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; ++d) {
                    const int nr = row + dr[d], nc = col + dc[d];
                    if (nr < 0 || nr >= H || nc < 0 || nc >= W) continue;
                    const std::size_t idx = static_cast<std::size_t>(nr * W + nc);
                    if (!s.shot[idx]) return static_cast<int>(idx);
                }
            }
        }

        std::vector<int> parity, any;
        for (int c = 0; c < inst.cellCount(); ++c) {
            if (s.shot[static_cast<std::size_t>(c)]) continue;
            any.push_back(c);
            if (((c / W) + (c % W)) % 2 == 0) parity.push_back(c);
        }
        const std::vector<int>& pick = parity.empty() ? any : parity;
        return pick[static_cast<std::size_t>(rng_.below(static_cast<int>(pick.size())))];
    }

private:
    detail::Stream rng_;
};

// The cheap engine: count legal placements of the remaining fleet through each
// cell, weighting placements that cover open hits. Deterministic.
class DensityPolicy : public Policy {
public:
    explicit DensityPolicy(std::uint64_t hitBonus = 50) : bonus_(hitBonus) {}
    [[nodiscard]] const char* name() const override { return "density"; }

    [[nodiscard]] int chooseShot(const Instance& inst, const History& h) override {
        const detail::Situation s(inst, h);
        const int W = inst.width, H = inst.height;
        std::vector<std::uint64_t> score(static_cast<std::size_t>(inst.cellCount()), 0);

        for (int L : s.remainingFleet) {
            for (int row = 0; row < H; ++row) {
                for (int col = 0; col < W; ++col) {
                    for (int axis = 0; axis < 2; ++axis) {
                        const bool horizontal = axis == 0;
                        if (horizontal ? col + L > W : row + L > H) continue;
                        if (L == 1 && !horizontal) continue;

                        std::uint64_t weight = 1;
                        bool legal = true;
                        int open = 0;
                        for (int k = 0; k < L; ++k) {
                            const std::size_t idx = static_cast<std::size_t>(
                                horizontal ? row * W + col + k : (row + k) * W + col);
                            if (s.miss[idx] || s.resolved[idx]) { legal = false; break; }
                            if (s.openHit[idx]) ++open;
                        }
                        if (!legal) continue;
                        for (int k = 0; k < open; ++k) weight *= bonus_;
                        for (int k = 0; k < L; ++k) {
                            const std::size_t idx = static_cast<std::size_t>(
                                horizontal ? row * W + col + k : (row + k) * W + col);
                            if (!s.shot[idx]) score[idx] += weight;
                        }
                    }
                }
            }
        }

        int best = -1;
        std::uint64_t bestScore = 0;
        for (int c = 0; c < inst.cellCount(); ++c) {
            if (s.shot[static_cast<std::size_t>(c)]) continue;
            const std::uint64_t v = score[static_cast<std::size_t>(c)];
            if (best < 0 || v > bestScore) { best = c; bestScore = v; }
        }
        return best >= 0 ? best : detail::firstUnshot(s);
    }

private:
    std::uint64_t bonus_;
};

// ---------------------------------------------------------------------------
// Exact tier: choose from the DP posterior.
// ---------------------------------------------------------------------------

enum class Objective { MaxHitProbability, MaxInformationGain };

class ExactPolicy : public Policy {
public:
    explicit ExactPolicy(Objective objective) : objective_(objective) {}

    [[nodiscard]] const char* name() const override {
        return objective_ == Objective::MaxHitProbability ? "exact-max-p" : "exact-max-info";
    }

    [[nodiscard]] int chooseShot(const Instance& inst, const History& h) override {
        std::uint64_t total = 0;
        const auto dist = outcomeDistribution(inst, h, total);
        int best = -1;
        double bestScore = -1.0;
        for (int c = 0; c < inst.cellCount(); ++c) {
            const auto& d = dist[static_cast<std::size_t>(c)];
            if (!d.shootable) continue;
            const double v = objective_ == Objective::MaxHitProbability ? d.hitProbability()
                                                                        : d.informationBits();
            if (v > bestScore + 1e-15) { bestScore = v; best = c; }
        }
        if (best >= 0) return best;
        for (int c = 0; c < inst.cellCount(); ++c) if (!h.shot(c)) return c;
        return -1;
    }

private:
    Objective objective_;
};

}  // namespace mayflower
