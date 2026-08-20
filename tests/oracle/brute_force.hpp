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

}  // namespace oracle
