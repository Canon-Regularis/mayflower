// Weighted counting: the same sweep, summing a product of weights over
// configurations instead of counting them. The result is a partition function,
// and integer exactness goes with it, so this path has its own validation
// regime.
//
// Two mechanisms compose multiplicatively.
//
//   per placement   a ship starting at cell o with length index li carries
//                   startH[o * nLengths + li] or startV[...], indexed exactly
//                   like Constraints::allowH. An opponent prior lives here: a
//                   log-linear model over placements is a set of these weights.
//
//   per cell        every cell contributes occupied[c] or empty[c] according to
//                   what it ends up as. An observation channel lives here: a
//                   noisy answer contributes its likelihood under each
//                   hypothesis, so the sweep returns the exact evidence and the
//                   posterior normaliser in one pass.
//
// The two are independent. Weighing placements alone reweights the prior;
// weighing cells alone conditions on soft evidence; both together give a
// posterior under a non-uniform prior.
//
// EXACTNESS
//
// Setting every weight to 1 must reproduce the plain count bit for bit, and it
// does, because every value a layer holds is a non-negative integer and every
// one of them stays below 2^53, where double addition of integers is exact.
//
// Note what that argument is NOT. A layer counts PARTIAL placements, and those
// are not bounded by the final count: constrain the standard board hard enough
// and the answer drops to a few hundred thousand while intermediate layers still
// carry billions. The real bound is the number of ways to place some subset of
// the fleet, at most the product over lengths of (placements + 1), which is
// about 8.0e10 for the standard instance against 9.0e15. That bound is
// instance-dependent, so weightedCount reports maxLayerSum and callers who care
// should check it rather than trust the argument. The standard instance measures
// 1.583e10, or 2^33.88.
//
// Away from that case the answer is floating point. A layer whose values
// threaten the exponent range is divided by a power of two, which edits
// exponents and leaves mantissas alone, so rescaling itself costs no precision.
// The result is reconstructed with ldexp rather than by multiplying by an
// exponential, so a scale beyond exp's range saturates correctly instead of
// producing infinity from finite parts. logTotal stays usable throughout.
#pragma once

#include <cstdint>
#include <vector>

#include "mayflower/instance.hpp"
#include "mayflower/profile_dp.hpp"

namespace mayflower {

// An empty vector means every weight in that family is 1, which is also the
// fast path: no lookup and no multiplication that could round.
struct Weights {
    std::vector<double> occupied;   // cellCount entries
    std::vector<double> empty;      // cellCount entries
    std::vector<double> startH;     // cellCount * nLengths entries
    std::vector<double> startV;     // cellCount * nLengths entries

    [[nodiscard]] bool trivial() const {
        return occupied.empty() && empty.empty() && startH.empty() && startV.empty();
    }

    // Every cell weighs the same whichever way it lands, so the sum is the count.
    static Weights uniform() { return Weights{}; }

    // A binary symmetric channel over an answered-cell record. `answers` holds
    // one entry per cell: -1 unanswered, 0 answered empty, 1 answered occupied.
    // An answer of eps = 0 is the truthful game and is better expressed as a
    // hard constraint, so eps is required to lie in (0, 1/2].
    static Weights noisyChannel(const Instance& inst, const std::vector<int>& answers,
                                double eps);

    // A log-linear prior over placements: weight exp(sum_k theta[k] f_k(placement)).
    // Features are supplied already evaluated, one score per placement slot, in
    // the same indexing as startH/startV. Passing all zeros gives the uniform
    // prior back exactly.
    static Weights fromLogPlacementScores(const Instance& inst,
                                          const std::vector<double>& logH,
                                          const std::vector<double>& logV);
};

struct WeightedResult {
    double total = 0;             // saturates to 0 or inf beyond double range
    double logTotal = 0;          // natural log, finite whenever the sum is
    bool rescaled = false;        // a layer was divided by a power of two
    double maxLayerSum = 0;       // largest sum over one layer, for the 2^53 argument
    std::size_t peakStates = 0;
    std::uint64_t edges = 0;
};

WeightedResult weightedCount(const Instance& inst, const Constraints& constraints,
                             const Weights& weights);
WeightedResult weightedCount(const Instance& inst, const Weights& weights);

// Exact posterior marginal that `cell` is occupied, as a constrained recount
// divided by the total. One sweep per cell, so weightedMarginals costs
// cellCount sweeps; the constrained ones are much cheaper than the free one.
double weightedMarginal(const Instance& inst, const Constraints& constraints,
                        const Weights& weights, int cell);
std::vector<double> weightedMarginals(const Instance& inst, const Constraints& constraints,
                                      const Weights& weights);

}  // namespace mayflower
