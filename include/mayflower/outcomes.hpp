// The exact six-outcome distribution for one shot.
//
// Shooting an unshot cell gives MISS, a plain HIT, or HIT plus SUNK(L). The
// split follows from the placement flows, so the channel costs nothing beyond
// the pass the marginals already paid for.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "mayflower/constraints.hpp"
#include "mayflower/observations.hpp"

namespace mayflower {

// ---------------------------------------------------------------------------
// One-ply outcome distribution.
//
// Shooting an unshot cell yields MISS, a plain HIT, or HIT plus SUNK(L). The
// ship covering the cell sinks exactly when every other cell of it has already
// been shot, which is a property of the placement, so the split follows from the
// placement flows.
//
// Because the outcome is determined by the hidden board, I(B; Y_c) is just the
// entropy of this distribution. That makes exact one-step information gain a
// by-product of the same sweep.
// ---------------------------------------------------------------------------

struct OutcomeDistribution {
    std::uint64_t miss = 0;
    std::uint64_t hit = 0;                    // occupied, ship survives
    std::array<std::uint64_t, 9> sunk{};      // indexed by ship length
    bool shootable = false;                   // false for already-shot cells

    [[nodiscard]] std::uint64_t total() const {
        std::uint64_t t = miss + hit;
        for (std::uint64_t v : sunk) t += v;
        return t;
    }
    // I(B; Y_c) in bits.
    [[nodiscard]] double informationBits() const;
    [[nodiscard]] double hitProbability() const;
};

std::vector<OutcomeDistribution> outcomeDistribution(const Instance& inst,
                                                     const History& history,
                                                     std::uint64_t& total);

}  // namespace mayflower
