#include "mayflower/exact_solver.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "mayflower/profile_dp.hpp"

namespace mayflower {
namespace {

using Mask = std::uint32_t;
using ConfigId = std::uint16_t;

// Outcome codes: 0 miss, 1 plain hit, 2+L a hit that sinks a length-L ship.
constexpr int kMiss = 0;
constexpr int kHit = 1;
constexpr int kSunkBase = 2;

struct World {
    int cells = 0;
    std::vector<Mask> occupancy;
    std::vector<std::vector<std::int8_t>> ship;   // ship index per cell
    std::vector<std::vector<Mask>> shipMask;
    std::vector<std::vector<std::int8_t>> shipLength;
};

World buildWorld(const Instance& inst, std::uint64_t configurationLimit) {
    if (inst.cellCount() > 32)
        throw std::invalid_argument("exact solver handles at most 32 cells");

    const Sampler sampler(inst);
    const std::uint64_t total = sampler.total();
    if (total > configurationLimit)
        throw std::invalid_argument("too many configurations for the exact solver");

    World w;
    w.cells = inst.cellCount();
    for (std::uint64_t r = 0; r < total; ++r) {
        const auto placements = sampler.unrank(r);
        Mask occ = 0;
        std::vector<std::int8_t> owner(static_cast<std::size_t>(w.cells), -1);
        std::vector<Mask> masks;
        std::vector<std::int8_t> lengths;
        for (std::size_t i = 0; i < placements.size(); ++i) {
            const ShipPlacement& p = placements[i];
            Mask m = 0;
            for (int k = 0; k < p.length; ++k) {
                const int cell = p.horizontal ? p.row * inst.width + p.col + k
                                              : (p.row + k) * inst.width + p.col;
                m |= Mask{1} << cell;
                owner[static_cast<std::size_t>(cell)] = static_cast<std::int8_t>(i);
            }
            occ |= m;
            masks.push_back(m);
            lengths.push_back(static_cast<std::int8_t>(p.length));
        }
        w.occupancy.push_back(occ);
        w.ship.push_back(std::move(owner));
        w.shipMask.push_back(std::move(masks));
        w.shipLength.push_back(std::move(lengths));
    }
    return w;
}

int outcomeOf(const World& w, ConfigId b, int cell, Mask shot) {
    const std::size_t i = b;
    if ((w.occupancy[i] & (Mask{1} << cell)) == 0) return kMiss;
    const int s = w.ship[i][static_cast<std::size_t>(cell)];
    const Mask others = w.shipMask[i][static_cast<std::size_t>(s)] & ~(Mask{1} << cell);
    if ((others & ~shot) != 0) return kHit;
    return kSunkBase + w.shipLength[i][static_cast<std::size_t>(s)];
}

// A chance node splits into at most miss, plain hit, and one sunk outcome per
// distinct ship length, so the branch table has a small fixed bound.
constexpr int kMaxCells = 32;      // buildWorld refuses larger boards
constexpr std::size_t kMaxBranches = 12;

struct Branch {
    const std::vector<ConfigId>* support;
    double weight;   // share of the support
    double floor;
};

struct StateKey {
    Mask shot = 0;
    std::vector<ConfigId> support;
    bool operator==(const StateKey& o) const {
        return shot == o.shot && support == o.support;
    }
};

struct StateHash {
    std::size_t operator()(const StateKey& k) const {
        std::uint64_t h = 0x9E3779B97F4A7C15ull ^ k.shot;
        for (ConfigId c : k.support)
            h ^= static_cast<std::uint64_t>(c) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        return static_cast<std::size_t>(h);
    }
};

struct Solver {
    const World& w;
    Adversary adversary;
    Pruning pruning;
    std::unordered_map<StateKey, double, StateHash> memo;
    std::uint64_t nodes = 0;
    std::uint64_t cellsPruned = 0;
    std::uint64_t branchesCut = 0;

    Solver(const World& world, Adversary adv, Pruning prune)
        : w(world), adversary(adv), pruning(prune) {
        memo.reserve(1 << 16);
    }

    // Every remaining ship cell must still be shot, so the mean count of unshot
    // ship cells is a floor on the expected remaining shots.
    [[nodiscard]] double floorOf(Mask shot, const std::vector<ConfigId>& support) const {
        std::uint64_t sum = 0;
        for (ConfigId b : support)
            sum += static_cast<std::uint64_t>(__builtin_popcount(w.occupancy[b] & ~shot));
        return static_cast<double>(sum) / static_cast<double>(support.size());
    }

    // When every surviving configuration occupies the same cells, their identity
    // no longer matters: the remaining ship cells are known and shooting them is
    // optimal. This collapses the whole tail of the game.
    [[nodiscard]] bool occupancySettled(const std::vector<ConfigId>& support) const {
        const Mask first = w.occupancy[support.front()];
        for (ConfigId b : support)
            if (w.occupancy[b] != first) return false;
        return true;
    }

    // Candidate shots, best first. A cell that is occupied in about half the
    // surviving configurations splits them most evenly and tends to be the
    // optimal move, so trying those first produces a tight incumbent on the
    // first cell rather than the last. Index order is kept for Pruning::None so
    // the reference search stays exactly what it was.
    mutable std::vector<double> scoreScratch;

    // Fills `out` with the unshot cells, best first, and returns how many. The
    // buffer is the caller's stack: a vector here costs one allocation per node,
    // and there are millions of nodes.
    int candidates(Mask shot, const std::vector<ConfigId>& support, int* out) const {
        int n = 0;
        for (int cell = 0; cell < w.cells; ++cell)
            if (((shot >> cell) & 1u) == 0) out[n++] = cell;
        if (pruning != Pruning::Star1) return n;

        // One pass over the support per node rather than one per cell: each
        // configuration contributes its whole occupancy word at once.
        const double total = static_cast<double>(support.size());
        std::vector<double>& score = scoreScratch;
        score.assign(static_cast<std::size_t>(w.cells), 0.0);
        for (ConfigId b : support) {
            Mask occ = w.occupancy[b] & ~shot;
            while (occ) {
                const int cell = __builtin_ctz(occ);
                occ &= occ - 1;
                score[static_cast<std::size_t>(cell)] += 1.0;
            }
        }
        for (double& v : score) v /= total;
        // Descending hit probability. The greedy shot is known to sit within a
        // fraction of a shot of optimal here, so it is a good first guess.
        std::stable_sort(out, out + n, [&](int a, int b) {
            return score[static_cast<std::size_t>(a)] > score[static_cast<std::size_t>(b)];
        });
        return n;
    }

    double value(Mask shot, const std::vector<ConfigId>& support, int* bestCell) {
        if (occupancySettled(support))
            return __builtin_popcount(w.occupancy[support.front()] & ~shot);

        StateKey key{shot, support};
        if (bestCell == nullptr) {
            const auto it = memo.find(key);
            if (it != memo.end()) return it->second;
        }
        ++nodes;

        double best = std::numeric_limits<double>::infinity();
        int bestAt = -1;
        const double n = static_cast<double>(support.size());

        int cellBuf[kMaxCells];
        const int cellCount = candidates(shot, support, cellBuf);
        for (int ci = 0; ci < cellCount; ++ci) {
            const int cell = cellBuf[ci];
            const Mask nextShot = shot | (Mask{1} << cell);

            std::map<int, std::vector<ConfigId>> branches;
            for (ConfigId b : support) branches[outcomeOf(w, b, cell, shot)].push_back(b);

            // Every branch is charged at its admissible floor to begin with, so
            // `running` is a lower bound on this cell's true score throughout.
            // Replacing a floor with the exact value only ever raises it.
            Branch ordered[kMaxBranches];
            std::size_t nb = 0;
            for (const auto& branch : branches) {
                ordered[nb++] = {&branch.second,
                                 static_cast<double>(branch.second.size()) / n,
                                 floorOf(nextShot, branch.second)};
            }

            if (pruning == Pruning::Star1) {   // branch ordering, Star1 only
                // Evaluate the branch with the largest floor first: in a worst
                // case it is the one that decides the node, and in an average it
                // moves `running` the most per unit of work.
                std::stable_sort(ordered, ordered + nb,
                                 [](const Branch& a, const Branch& b) {
                                     return a.floor > b.floor;
                                 });
            }

            double running = 1.0;
            if (adversary == Adversary::Committed) {
                for (std::size_t i = 0; i < nb; ++i) running += ordered[i].weight * ordered[i].floor;
            } else {
                double worst = 0;
                for (std::size_t i = 0; i < nb; ++i) worst = std::max(worst, ordered[i].floor);
                running += worst;
            }
            if (running >= best) { ++cellsPruned; continue; }

            double score;
            if (pruning == Pruning::None && adversary == Adversary::Committed) {
                // The original: unevaluated branches count for nothing, so the
                // partial sum is a weaker bound and cuts later.
                double partial = 1.0;
                bool cut = false;
                for (std::size_t i = 0; i < nb; ++i) {
                    partial += ordered[i].weight * value(nextShot, *ordered[i].support, nullptr);
                    if (partial >= best && i + 1 < nb) { cut = true; break; }
                }
                if (cut) { ++branchesCut; continue; }
                score = partial;
            } else if (adversary == Adversary::Committed) {
                bool cut = false;
                for (std::size_t i = 0; i < nb; ++i) {
                    const Branch& b = ordered[i];
                    const double exact = value(nextShot, *b.support, nullptr);
                    running += b.weight * (exact - b.floor);
                    if (running >= best && i + 1 < nb) {
                        cut = true;
                        break;
                    }
                }
                if (cut) { ++branchesCut; continue; }
                score = running;
            } else {
                // The hider answers to hurt most, so the chance node maximises.
                // Branches still to come cannot pull the maximum down, so the
                // running bound is the largest of what is known and what is
                // still floored.
                double valueOf[kMaxBranches] = {0};
                bool cut = false;
                for (std::size_t i = 0; i < nb; ++i) {
                    valueOf[i] = value(nextShot, *ordered[i].support, nullptr);
                    double bound = 0;
                    for (std::size_t j = 0; j < nb; ++j)
                        bound = std::max(bound, j <= i ? valueOf[j] : ordered[j].floor);
                    if (1.0 + bound >= best && i + 1 < nb) {
                        cut = true;
                        break;
                    }
                }
                if (cut) { ++branchesCut; continue; }
                double worst = 0;
                for (std::size_t i = 0; i < nb; ++i) worst = std::max(worst, valueOf[i]);
                score = 1.0 + worst;
            }

            if (score < best) {
                best = score;
                bestAt = cell;
            }
        }

        if (bestCell != nullptr) *bestCell = bestAt;
        else memo.emplace(std::move(key), best);
        return best;
    }
};

}  // namespace

ExactSolution solveOptimal(const Instance& inst, std::uint64_t configurationLimit,
                           Adversary adversary, Pruning pruning) {
    const auto t0 = std::chrono::steady_clock::now();
    const World w = buildWorld(inst, configurationLimit);

    std::vector<ConfigId> all(w.occupancy.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<ConfigId>(i);

    Solver solver(w, adversary, pruning);
    ExactSolution out;
    out.configurations = w.occupancy.size();
    out.expectedShots = solver.value(0, all, &out.optimalFirstShot);
    out.memoStates = solver.memo.size();
    out.nodesExpanded = solver.nodes;
    out.cellsPruned = solver.cellsPruned;
    out.branchesCut = solver.branchesCut;
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

PolicyExpectation exactPolicyExpectation(const Instance& inst, Policy& policy,
                                        std::uint64_t seed) {
    const auto t0 = std::chrono::steady_clock::now();
    const Sampler sampler(inst);
    const std::uint64_t total = sampler.total();

    PolicyExpectation out;
    out.configurations = total;
    out.best = inst.cellCount() + 1;
    std::uint64_t sum = 0;
    for (std::uint64_t r = 0; r < total; ++r) {
        const int shots = playGame(inst, sampler.unrank(r), policy, seed).shots;
        sum += static_cast<std::uint64_t>(shots);
        out.worst = std::max(out.worst, shots);
        out.best = std::min(out.best, shots);
    }
    out.expectedShots = static_cast<double>(sum) / static_cast<double>(total);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

}  // namespace mayflower
