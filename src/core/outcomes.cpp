// The six-outcome distribution for one shot.
//
// One forward-backward pass already knows, for every cell, how many
// configurations occupy it and how each sunk length would be reported. So the
// exact one-ply channel costs nothing beyond the pass the marginals already
// paid for, which is what makes exact information gain affordable.
#include "mayflower/profile_dp.hpp"


#include <cmath>
#include <cstdint>
#include <vector>

namespace mayflower {

double OutcomeDistribution::hitProbability() const {
    const std::uint64_t t = total();
    if (t == 0) return 0.0;
    return static_cast<double>(t - miss) / static_cast<double>(t);
}

double OutcomeDistribution::informationBits() const {
    const std::uint64_t t = total();
    if (t == 0) return 0.0;
    double h = 0.0;
    const auto term = [&](std::uint64_t n) {
        if (n == 0 || n == t) return;
        const double q = static_cast<double>(n) / static_cast<double>(t);
        h -= q * std::log2(q);
    };
    term(miss);
    term(hit);
    for (std::uint64_t v : sunk) term(v);
    return h;
}

std::vector<OutcomeDistribution> outcomeDistribution(const Instance& inst,
                                                     const History& history,
                                                     std::uint64_t& total) {
    const Constraints constraints = constraintsFrom(inst, history);
    const LatticeFlows flows = analyse(inst, constraints);
    total = flows.total;

    const int W = inst.width, H = inst.height;
    const std::vector<int> lengths = inst.distinctLengths();
    std::vector<OutcomeDistribution> out(static_cast<std::size_t>(inst.cellCount()));
    if (flows.total == 0) return out;

    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            const int cell = row * W + col;
            OutcomeDistribution& d = out[static_cast<std::size_t>(cell)];
            if (history.shot(cell)) continue;
            d.shootable = true;
            d.miss = flows.total - flows.occupancy[static_cast<std::size_t>(cell)];

            for (std::size_t li = 0; li < lengths.size(); ++li) {
                const int L = lengths[li];
                for (int k = 0; k < L; ++k) {   // horizontal placements covering the cell
                    const int c0 = col - k;
                    if (c0 < 0 || c0 + L > W) continue;
                    const std::uint64_t f =
                        flows.placement[placementIndex(inst, row, c0, static_cast<int>(li), true)];
                    if (f == 0) continue;
                    bool sinks = true;
                    for (int t = 0; t < L && sinks; ++t) {
                        const int other = row * W + c0 + t;
                        if (other != cell && !history.shot(other)) sinks = false;
                    }
                    if (sinks) d.sunk[static_cast<std::size_t>(L)] += f;
                    else       d.hit += f;
                }
                for (int k = 0; k < L; ++k) {   // vertical placements covering the cell
                    const int r0 = row - k;
                    if (r0 < 0 || r0 + L > H) continue;
                    const std::uint64_t f =
                        flows.placement[placementIndex(inst, r0, col, static_cast<int>(li), false)];
                    if (f == 0) continue;
                    bool sinks = true;
                    for (int t = 0; t < L && sinks; ++t) {
                        const int other = (r0 + t) * W + col;
                        if (other != cell && !history.shot(other)) sinks = false;
                    }
                    if (sinks) d.sunk[static_cast<std::size_t>(L)] += f;
                    else       d.hit += f;
                }
            }
        }
    }
    return out;
}

}  // namespace mayflower
