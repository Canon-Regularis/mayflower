// Independent brute-force oracle.
//
// Includes nothing from include/mayflower or src/. Written from scratch against
// standard-library types so that agreement with the profile DP is evidence.
// Do not refactor it to share code with the engine.
//
// Enumerates every choice of one placement per ship with all placements pairwise
// disjoint, treating equal-length ships as interchangeable, so it counts the same
// object as the DP: physical boards.
//
// Feasible to roughly 6x6 with a three-ship fleet.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace oracle {

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"   // __int128 is a GCC extension
#endif
using Mask = unsigned __int128;
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

// Placements of a length-L ship, as cell-set bitmasks with cell (r,c) at bit r*W+c.
inline std::vector<Mask> placements(int W, int H, int L) {
    std::vector<Mask> out;
    for (int r = 0; r < H; ++r) {
        for (int c = 0; c + L <= W; ++c) {
            Mask m = 0;
            for (int k = 0; k < L; ++k) m |= Mask{1} << (r * W + c + k);
            out.push_back(m);
        }
    }
    if (L > 1) {   // a length-1 ship would be counted twice
        for (int c = 0; c < W; ++c) {
            for (int r = 0; r + L <= H; ++r) {
                Mask m = 0;
                for (int k = 0; k < L; ++k) m |= Mask{1} << ((r + k) * W + c);
                out.push_back(m);
            }
        }
    }
    return out;
}

namespace detail {

struct Group {
    std::vector<Mask> options;
    int multiplicity = 0;
};

inline std::vector<Group> buildGroups(int W, int H, std::vector<int> fleet) {
    std::sort(fleet.begin(), fleet.end());
    std::vector<Group> groups;
    for (std::size_t i = 0; i < fleet.size();) {
        std::size_t j = i;
        while (j < fleet.size() && fleet[j] == fleet[i]) ++j;
        groups.push_back({placements(W, H, fleet[i]), static_cast<int>(j - i)});
        i = j;
    }
    return groups;
}

// Strictly increasing option indices within a group make equal-length ships
// interchangeable.
inline std::uint64_t recurse(const std::vector<Group>& groups, std::size_t gi, int need,
                             std::size_t from, Mask occupied) {
    if (gi == groups.size()) return 1;
    if (need == 0) {
        std::size_t next = gi + 1;
        return recurse(groups, next, next < groups.size() ? groups[next].multiplicity : 0,
                       0, occupied);
    }
    const std::vector<Mask>& options = groups[gi].options;
    std::uint64_t total = 0;
    for (std::size_t i = from; i + static_cast<std::size_t>(need) <= options.size(); ++i) {
        if ((occupied & options[i]) != 0) continue;
        total += recurse(groups, gi, need - 1, i + 1, occupied | options[i]);
    }
    return total;
}

}  // namespace detail

inline std::uint64_t bruteForceCount(int W, int H, std::vector<int> fleet) {
    const std::vector<detail::Group> groups = detail::buildGroups(W, H, std::move(fleet));
    if (groups.empty()) return 1;
    return detail::recurse(groups, 0, groups[0].multiplicity, 0, Mask{0});
}

// Boards in which cell (row,col) is occupied.
inline std::uint64_t bruteForceOccupancy(int W, int H, std::vector<int> fleet,
                                         int row, int col) {
    const std::vector<detail::Group> groups = detail::buildGroups(W, H, std::move(fleet));
    const Mask target = Mask{1} << (row * W + col);
    if (groups.empty()) return 0;

    std::uint64_t total = 0;
    auto go = [&](auto&& self, std::size_t gi, int need, std::size_t from, Mask occ) -> void {
        if (gi == groups.size()) {
            if ((occ & target) != 0) ++total;
            return;
        }
        if (need == 0) {
            std::size_t next = gi + 1;
            self(self, next, next < groups.size() ? groups[next].multiplicity : 0, 0, occ);
            return;
        }
        const std::vector<Mask>& options = groups[gi].options;
        for (std::size_t i = from; i + static_cast<std::size_t>(need) <= options.size(); ++i) {
            if ((occ & options[i]) != 0) continue;
            self(self, gi, need - 1, i + 1, occ | options[i]);
        }
    };
    go(go, 0, groups[0].multiplicity, 0, Mask{0});
    return total;
}


// ---------------------------------------------------------------------------
// Ordered observation model.
//
// A ship reports SUNK on the shot that completes it, and only then. Replaying a
// history against a fully known board is the definition the engine must match.
// ---------------------------------------------------------------------------

inline int popcount128(Mask m) {
    return __builtin_popcountll(static_cast<unsigned long long>(m)) +
           __builtin_popcountll(static_cast<unsigned long long>(m >> 64));
}

using BoardShips = std::vector<Mask>;

// Every physical board, as its list of ship masks.
inline std::vector<BoardShips> enumerateBoards(int W, int H, std::vector<int> fleet) {
    const std::vector<detail::Group> groups = detail::buildGroups(W, H, std::move(fleet));
    std::vector<BoardShips> boards;
    if (groups.empty()) return boards;

    BoardShips chosen;
    auto go = [&](auto&& self, std::size_t gi, int need, std::size_t from, Mask occ) -> void {
        if (gi == groups.size()) {
            boards.push_back(chosen);
            return;
        }
        if (need == 0) {
            std::size_t next = gi + 1;
            self(self, next, next < groups.size() ? groups[next].multiplicity : 0, 0, occ);
            return;
        }
        const std::vector<Mask>& options = groups[gi].options;
        for (std::size_t i = from; i + static_cast<std::size_t>(need) <= options.size(); ++i) {
            if ((occ & options[i]) != 0) continue;
            chosen.push_back(options[i]);
            self(self, gi, need - 1, i + 1, occ | options[i]);
            chosen.pop_back();
        }
    };
    go(go, 0, groups[0].multiplicity, 0, Mask{0});
    return boards;
}

enum class Outcome : int { Miss = 0, Hit = 1, Sunk = 2 };

struct Observation {
    Outcome outcome = Outcome::Miss;
    int sunkLength = 0;

    friend bool operator==(const Observation& a, const Observation& b) {
        return a.outcome == b.outcome && a.sunkLength == b.sunkLength;
    }
};

// Play `shots` (cell indices) against a board.
inline std::vector<Observation> simulate(const BoardShips& ships,
                                         const std::vector<int>& shots) {
    std::vector<int> remaining;
    remaining.reserve(ships.size());
    for (const Mask& s : ships) remaining.push_back(popcount128(s));

    std::vector<Observation> out;
    out.reserve(shots.size());
    for (int cell : shots) {
        const Mask bit = Mask{1} << cell;
        std::size_t idx = ships.size();
        for (std::size_t i = 0; i < ships.size(); ++i) {
            if ((ships[i] & bit) != 0) { idx = i; break; }
        }
        if (idx == ships.size()) {
            out.push_back({Outcome::Miss, 0});
            continue;
        }
        if (--remaining[idx] == 0) out.push_back({Outcome::Sunk, popcount128(ships[idx])});
        else                       out.push_back({Outcome::Hit, 0});
    }
    return out;
}

// Boards consistent with the ordered record.
inline std::uint64_t posteriorCount(const std::vector<BoardShips>& boards,
                                    const std::vector<int>& shots,
                                    const std::vector<Observation>& observed) {
    std::uint64_t n = 0;
    for (const BoardShips& b : boards) {
        if (simulate(b, shots) == observed) ++n;
    }
    return n;
}

}  // namespace oracle
