// The enumeration machinery the extensions share.
//
// Backtracker places ships one at a time and abandons dead branches, which is
// the opposite cost profile to a counting sweep and is what makes the two
// comparable on identical records. Five of the eight extensions drive it, and
// so does the self test, so it is the shared vocabulary rather than any one
// experiment's private tool.
//
// Internal to the m9 tool.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <set>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

#include "mayflower/certify.hpp"
#include "mayflower/constants.hpp"
#include "mayflower/exact_solver.hpp"
#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"
#include "mayflower/random.hpp"


#include "sections.hpp"

namespace mayflower::m9 {

// The node budget one backtracking run may spend before it counts as
// capped. Read by the density sweep and by the self test that bounds it.
constexpr std::uint64_t kNodeCap = 20'000'000ull;

inline int popcount64(std::uint64_t x) {
    return __builtin_popcountll(x);
}

using mayflower::Rng;

// A backtracking feasibility search over the same records, for contrast.
//
// The DP is a counting sweep: it visits every layer once whatever the record
// says, so its cost tracks how many boundary states survive and nothing else.
// A search that places ships one at a time and abandons dead branches has the
// opposite profile, so running both on identical records separates the two.
struct Backtracker {
    const Instance& inst;
    const std::vector<CellConstraint>& cells;
    std::vector<std::vector<std::uint64_t>> options;  // per fleet slot
    std::uint64_t hitMask = 0;
    std::uint64_t nodes = 0;
    std::uint64_t cap;

    Backtracker(const Instance& i, const std::vector<CellConstraint>& c, std::uint64_t nodeCap)
        : inst(i), cells(c), cap(nodeCap) {
        std::uint64_t banned = 0;
        for (int k = 0; k < inst.cellCount(); ++k) {
            if (cells[static_cast<std::size_t>(k)] == CellConstraint::MustBeEmpty)
                banned |= 1ull << k;
            if (cells[static_cast<std::size_t>(k)] == CellConstraint::MustBeOccupied)
                hitMask |= 1ull << k;
        }
        for (int L : inst.fleet) {
            std::vector<std::uint64_t> masks;
            for (int r = 0; r < inst.height; ++r)
                for (int c2 = 0; c2 + L <= inst.width; ++c2) {
                    std::uint64_t m = 0;
                    for (int t = 0; t < L; ++t) m |= 1ull << inst.cellIndex(r, c2 + t);
                    if (!(m & banned)) masks.push_back(m);
                }
            if (L > 1)
                for (int r = 0; r + L <= inst.height; ++r)
                    for (int c2 = 0; c2 < inst.width; ++c2) {
                        std::uint64_t m = 0;
                        for (int t = 0; t < L; ++t) m |= 1ull << inst.cellIndex(r + t, c2);
                        if (!(m & banned)) masks.push_back(m);
                    }
            options.push_back(std::move(masks));
        }
    }

    // Slots hold the fleet in non-increasing length order, so equal lengths sit
    // next to each other and forcing their placement indices to increase kills
    // the duplicate branches without changing feasibility.
    bool search(std::size_t slot, std::uint64_t used, std::size_t from, int remainingCells) {
        if (nodes >= cap) return false;
        if (slot == options.size()) return (hitMask & ~used) == 0;

        // Every hit still uncovered has to be paid for by a later ship.
        if (popcount64(hitMask & ~used) > remainingCells) return false;

        const bool sameAsPrevious =
            slot > 0 && inst.fleet[slot] == inst.fleet[slot - 1];
        const auto& masks = options[slot];
        for (std::size_t i = sameAsPrevious ? from : 0; i < masks.size(); ++i) {
            if (masks[i] & used) continue;
            ++nodes;
            if (nodes >= cap) return false;
            if (search(slot + 1, used | masks[i], i + 1,
                       remainingCells - inst.fleet[slot]))
                return true;
        }
        return false;
    }

    bool solve() {
        return search(0, 0, 0, inst.shipCells());
    }
};

// 3. The adaptivity gap.
//
// A non-adaptive player commits to one order of the cells before play and reads
// nothing back. The game still ends when the last ship cell is shot, so for a
// fixed order the clearing time is the position of the board's last occupied
// cell, and
//
//   E[T] = sum_{t=0}^{n-1} P(the first t cells do not cover the board)
//        = n - (1/N) sum_{t=0}^{n-1} c(S_t),
//
// with S_t the first t cells and c(S) the number of configurations inside S.
// Every term depends on the prefix as a set, so the best order is the best chain
// through the subset lattice, and that is a DP over 2^n states rather than a
// search over n! orders. The cell-covering objective makes this the full-cover
// variant of min-sum set cover.
//
// c(S) for all S at once is a subset-sum transform over the configuration masks.
inline void enumerateMasks(const Backtracker& bt, std::size_t slot, std::uint64_t used,
                    std::size_t from, std::vector<std::uint64_t>& out) {
    if (slot == bt.options.size()) { out.push_back(used); return; }
    const bool sameAsPrevious =
        slot > 0 && bt.inst.fleet[slot] == bt.inst.fleet[slot - 1];
    const auto& masks = bt.options[slot];
    for (std::size_t i = sameAsPrevious ? from : 0; i < masks.size(); ++i) {
        if (masks[i] & used) continue;
        enumerateMasks(bt, slot + 1, used | masks[i], i + 1, out);
    }
}

}  // namespace mayflower::m9
