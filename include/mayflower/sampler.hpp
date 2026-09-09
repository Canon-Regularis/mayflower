// Exact uniform sampling by unranking the lattice.
//
// The board generator for every statistic downstream. Sequential rejection
// placement is not uniform, so this is a hard gate rather than a convenience.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mayflower/constraints.hpp"

namespace mayflower {

// ---------------------------------------------------------------------------
// Exact uniform sampling by unranking.
//
// The lattice is a layered DAG in which every configuration is one source-to-
// sink path. Weighting each edge by the number of completions below it turns
// rank r in [0, |Omega|) into a path, so unrank() is a bijection from ranks to
// configurations. Drawing r uniformly therefore samples Omega uniformly, with
// no rejection and no MCMC.
//
// This is what the board generator must use. Sequential rejection placement
// (place the 5, then the 4, and so on) is not uniform: it over-weights
// configurations that leave room for the later ships.
// ---------------------------------------------------------------------------

struct ShipPlacement {
    int  row = 0;          // topmost cell for vertical, the row for horizontal
    int  col = 0;          // leftmost cell for horizontal, the column for vertical
    int  length = 0;
    bool horizontal = true;
};

class Sampler {
public:
    Sampler(const Instance& inst, const Constraints& constraints);
    explicit Sampler(const Instance& inst);
    ~Sampler();
    Sampler(Sampler&&) noexcept;
    Sampler& operator=(Sampler&&) noexcept;

    [[nodiscard]] std::uint64_t total() const;

    // rank must lie in [0, total()). Throws otherwise.
    [[nodiscard]] std::vector<ShipPlacement> unrank(std::uint64_t rank) const;

    // Number of stored backward-count entries, for memory reporting.
    [[nodiscard]] std::size_t storedEntries() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mayflower
