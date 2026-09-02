// Ordered observation record and the placement predicate it induces.
//
// Order is part of the record. SUNK(x, L) means the shot at x sank the ship, so
// every other cell of that ship was shot strictly before x, and two orderings of
// one shot multiset can leave different configurations standing.
//
// The whole observation model reduces to two things the DP can consume:
//   1. a per-cell constraint (MISS forbids occupancy, HIT/SUNK requires it);
//   2. a predicate on each candidate ship placement, evaluated once per
//      placement when the DP starts a ship.
//
// The predicate, for a placement with cell set S of length L:
//   if every cell of S has been shot, the ship is fully destroyed, so the
//   latest-shot cell of S must carry SUNK with length L and every other cell of
//   S must carry a plain HIT;
//   otherwise the ship survives, so no cell of S may carry SUNK, and every shot
//   cell of S must carry a plain HIT.
//
// Order enters through "latest-shot cell of S", which is why a set-valued record
// is insufficient. See docs/ORDER_DEPENDENCE.md.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "mayflower/instance.hpp"

namespace mayflower {

enum class Outcome : std::uint8_t { Miss = 0, Hit, Sunk };

class History {
public:
    explicit History(const Instance& inst)
        : width_(inst.width),
          time_(static_cast<std::size_t>(inst.cellCount()), -1),
          outcome_(static_cast<std::size_t>(inst.cellCount()), Outcome::Miss),
          sunkLength_(static_cast<std::size_t>(inst.cellCount()), 0) {}

    // Append a shot. `sunkLength` is read only when `outcome` is Sunk.
    void add(int row, int col, Outcome outcome, int sunkLength = 0) {
        // The column is bounded before the index is flattened. Checking only the
        // flat index lets a column outside the row slide into the next one:
        // add(0, 15) on a width-10 board recorded cell 15, and add(1, -5)
        // recorded cell 5, both silently and both a different cell than asked
        // for.
        if (row < 0 || col < 0 || col >= width_)
            throw std::out_of_range("cell out of range");
        const std::size_t c = static_cast<std::size_t>(row * width_ + col);
        if (c >= time_.size()) throw std::out_of_range("cell out of range");
        if (time_[c] >= 0) throw std::invalid_argument("cell shot twice");
        if (outcome == Outcome::Sunk && sunkLength < 1)
            throw std::invalid_argument("SUNK needs a ship length");
        time_[c] = next_++;
        outcome_[c] = outcome;
        sunkLength_[c] = sunkLength;
        sequence_.push_back(row * width_ + col);
    }

    [[nodiscard]] int  size() const { return next_; }
    // Cells in the order they were shot.
    [[nodiscard]] const std::vector<int>& sequence() const { return sequence_; }
    [[nodiscard]] int sunkLength(int cell) const {
        return sunkLength_[static_cast<std::size_t>(cell)];
    }
    [[nodiscard]] bool shot(int cell) const { return time_[static_cast<std::size_t>(cell)] >= 0; }
    [[nodiscard]] Outcome outcome(int cell) const { return outcome_[static_cast<std::size_t>(cell)]; }
    [[nodiscard]] int  shotTime(int cell) const { return time_[static_cast<std::size_t>(cell)]; }

    // The placement predicate described above. `footprint` holds L cell indices.
    [[nodiscard]] bool allowsPlacement(const int* footprint, int length) const {
        int shotCount = 0;
        int latest = -1;
        int latestCell = -1;
        for (int k = 0; k < length; ++k) {
            const std::size_t c = static_cast<std::size_t>(footprint[k]);
            if (time_[c] < 0) continue;
            if (outcome_[c] == Outcome::Miss) return false;
            ++shotCount;
            if (time_[c] > latest) {
                latest = time_[c];
                latestCell = footprint[k];
            }
        }
        if (shotCount == length) {
            const std::size_t lc = static_cast<std::size_t>(latestCell);
            if (outcome_[lc] != Outcome::Sunk) return false;
            if (sunkLength_[lc] != length) return false;
            for (int k = 0; k < length; ++k) {
                if (footprint[k] == latestCell) continue;
                if (outcome_[static_cast<std::size_t>(footprint[k])] != Outcome::Hit) return false;
            }
            return true;
        }
        for (int k = 0; k < length; ++k) {
            const std::size_t c = static_cast<std::size_t>(footprint[k]);
            if (time_[c] >= 0 && outcome_[c] != Outcome::Hit) return false;
        }
        return true;
    }

private:
    int width_;
    int next_ = 0;
    std::vector<int>      sequence_;
    std::vector<int>      time_;        // -1 when unshot
    std::vector<Outcome>  outcome_;
    std::vector<int>      sunkLength_;
};

}  // namespace mayflower
