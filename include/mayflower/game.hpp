// Game harness: play a policy against a known board and count the shots.
//
// The policy receives the Instance and the History only. It never sees the
// board, so the information set is enforced by the type system.
#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/observations.hpp"
#include "mayflower/profile_dp.hpp"

namespace mayflower {

class Policy {
public:
    virtual ~Policy() = default;
    [[nodiscard]] virtual const char* name() const = 0;
    virtual void reset(const Instance&, std::uint64_t /*seed*/) {}
    // Must return an unshot cell index.
    [[nodiscard]] virtual int chooseShot(const Instance&, const History&) = 0;
};

struct GameResult {
    int shots = 0;
    int misses = 0;
};

// Play `policy` against `truth`, writing the ordered record into `history`.
inline GameResult playGameTraced(const Instance& inst,
                                 const std::vector<ShipPlacement>& truth,
                                 Policy& policy,
                                 std::uint64_t seed,
                                 History& history);

// Play `policy` against `truth` until every ship cell has been shot.
inline GameResult playGame(const Instance& inst,
                           const std::vector<ShipPlacement>& truth,
                           Policy& policy,
                           std::uint64_t seed = 0) {
    History discard(inst);
    return playGameTraced(inst, truth, policy, seed, discard);
}

inline GameResult playGameTraced(const Instance& inst,
                                 const std::vector<ShipPlacement>& truth,
                                 Policy& policy,
                                 std::uint64_t seed,
                                 History& history) {
    const int W = inst.width;
    const std::size_t cells = static_cast<std::size_t>(inst.cellCount());

    std::vector<int> shipAt(cells, -1);
    std::vector<int> remaining(truth.size(), 0);
    for (std::size_t i = 0; i < truth.size(); ++i) {
        const ShipPlacement& s = truth[i];
        remaining[i] = s.length;
        for (int k = 0; k < s.length; ++k) {
            const int cell = s.horizontal ? s.row * W + s.col + k : (s.row + k) * W + s.col;
            if (shipAt[static_cast<std::size_t>(cell)] >= 0)
                throw std::invalid_argument("board has overlapping ships");
            shipAt[static_cast<std::size_t>(cell)] = static_cast<int>(i);
        }
    }

    policy.reset(inst, seed);
    GameResult result;
    int hitsNeeded = inst.shipCells();

    while (hitsNeeded > 0) {
        const int cell = policy.chooseShot(inst, history);
        if (cell < 0 || cell >= inst.cellCount())
            throw std::logic_error(std::string(policy.name()) + " returned an out-of-range cell");
        if (history.shot(cell))
            throw std::logic_error(std::string(policy.name()) + " reshot a cell");

        ++result.shots;
        const int ship = shipAt[static_cast<std::size_t>(cell)];
        if (ship < 0) {
            ++result.misses;
            history.add(cell / W, cell % W, Outcome::Miss);
            continue;
        }
        --hitsNeeded;
        if (--remaining[static_cast<std::size_t>(ship)] == 0)
            history.add(cell / W, cell % W, Outcome::Sunk, truth[static_cast<std::size_t>(ship)].length);
        else
            history.add(cell / W, cell % W, Outcome::Hit);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Seeded board bank.
//
// Boards come from the DP unranker, so the pool is exactly uniform over the
// configuration space. Every policy plays the same seeds, which is what makes
// the comparisons paired.
// ---------------------------------------------------------------------------

class BoardBank {
public:
    BoardBank(const Instance& inst, std::uint64_t key)
        : sampler_(inst), key_(key), total_(sampler_.total()) {
        // An instance can validate and still admit nothing: 2x2 {2,2,2} passes,
        // since every ship fits the board on its own, but six cells will not go
        // into four. rankFor takes UINT64_MAX % total_, so drawing from such a
        // bank divided by zero instead of saying there was nothing to draw.
        if (total_ == 0)
            throw std::invalid_argument(inst.describe() +
                                        " admits no configuration to draw from");
    }

    [[nodiscard]] std::uint64_t total() const { return total_; }

    // Board for a game id. Depends only on (key, id), never on call order, so
    // policies stay aligned no matter how many randoms they draw.
    [[nodiscard]] std::vector<ShipPlacement> board(std::uint64_t gameId) const {
        return sampler_.unrank(rankFor(gameId));
    }

private:
    [[nodiscard]] std::uint64_t rankFor(std::uint64_t gameId) const {
        std::uint64_t x = gameId + key_;
        const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % total_) - 1;
        while (true) {
            x += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            if (z <= limit) return z % total_;
        }
    }

    Sampler sampler_;
    std::uint64_t key_;
    std::uint64_t total_;
};

}  // namespace mayflower
