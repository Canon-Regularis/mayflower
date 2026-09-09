// The fleet counter, once.
//
// Four sweeps carried this struct: profile_dp.cpp, weighted.cpp, notouch.cpp and
// profile_dp_blocked.cpp. Two of the copies differed from each other by a single
// blank line, and all four differed from the reference only by a const and the
// placement of one brace. Token for token they were the same code.
//
// profile_dp_fast.cpp holds a fifth copy that is genuinely different: it flattens
// the same mixed radix into a table of pre shifted key increments so that
// starting a ship costs one addition. That is the rung's optimisation and it
// stays where it is; it builds its table from this rather than restating the
// radix.
//
// What this counts: the fleet is a multiset, so the state is how many ships of
// each distinct length have been placed. Storing that as a mixed radix integer
// keeps ship identity out of the boundary profile, which is why the sweep counts
// unordered configurations and never has to divide by a factorial.
#pragma once

#include <cstddef>
#include <vector>

#include "mayflower/instance.hpp"

namespace mayflower::detail {

struct FleetCounter {
    std::vector<int> lengths;      // ascending distinct lengths
    std::vector<int> caps;         // how many of each length the fleet holds
    std::vector<int> radixStride;
    int stateCount = 1;
    int fullIndex = 0;
    std::vector<int> addTable;     // [state * nLengths + li] -> new state, or -1

    explicit FleetCounter(const Instance& inst)
        : lengths(inst.distinctLengths()), caps(inst.multiplicities()) {
        radixStride.resize(lengths.size());
        int stride = 1;
        for (std::size_t i = 0; i < lengths.size(); ++i) {
            radixStride[i] = stride;
            stride *= caps[i] + 1;
        }
        stateCount = stride;
        fullIndex = stateCount - 1;

        // Precomputed because the sweep asks this once per emitted edge, and
        // there are about 2.9e7 of them on the standard instance.
        addTable.assign(static_cast<std::size_t>(stateCount) * lengths.size(), -1);
        for (int s = 0; s < stateCount; ++s) {
            for (std::size_t li = 0; li < lengths.size(); ++li) {
                const int used = (s / radixStride[li]) % (caps[li] + 1);
                addTable[static_cast<std::size_t>(s) * lengths.size() + li] =
                    (used < caps[li]) ? s + radixStride[li] : -1;
            }
        }
    }

    // The state after starting one ship of distinct length index li, or -1 when
    // the fleet has none of that length left.
    [[nodiscard]] int afterStarting(int state, std::size_t li) const {
        return addTable[static_cast<std::size_t>(state) * lengths.size() + li];
    }
};

}  // namespace mayflower::detail
