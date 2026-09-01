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
// Exactness, and where it stops.
//
// Setting every weight to 1 must reproduce the plain count bit for bit, and it
// does, because every value a layer holds is a non-negative integer and every
// one of them stays below 2^53, where double addition of integers is exact.
//
// The bound is on layers, not on the answer. A layer counts partial placements,
// which the final count does not bound: constrain the standard board hard enough
// and it answers in the hundreds of thousands while intermediate layers still
// carry billions. The bound is the number of ways to place some subset of the
// fleet, at most the product over lengths of (placements + 1), about 8.0e10 for
// the standard instance against 2^53. It is instance-dependent, so weightedCount
// reports the measured maxLayerSum, 1.583e10 here, and the exact flag is derived
// from that rather than from the argument.
//
// Away from unit weights the answer is floating point, and the failure is
// one-sided. A layer whose maximum threatens the exponent range is divided by a
// power of two, which is exact while the result stays normal. It cannot stay
// normal indefinitely: the scale is one power of two for the whole layer, so a
// value 1e308 behind the maximum reaches zero when the maximum is brought back
// into range, and the guard watches only the maximum. Weights whose live range
// exceeds a double therefore lose configurations silently, and underflowed is
// set at the multiply and at the rescale to say so. weightedMarginals refuses
// outright, since the forward and backward values are each representable where
// their product is not.
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

    // Two non-zero factors gave zero, so a configuration was dropped and total
    // is a lower bound. Detected at the multiply and at the rescale, so it does
    // not depend on rescaled, which a decaying record never trips.
    bool underflowed = false;

    // True when this particular run is exact rather than approximate: the
    // weights were all 1, so every value was a non-negative integer, no layer
    // sum reached 2^53, and neither a rescale nor an underflow intervened. Both
    // of the last two are checked, since a decaying record can underflow without
    // ever rescaling. The bound in the comment
    // above is instance-dependent, so the run checks itself rather than relying
    // on the argument holding for whatever instance a caller passes.
    bool exact = false;
    std::size_t peakStates = 0;
    std::uint64_t edges = 0;
};

WeightedResult weightedCount(const Instance& inst, const Constraints& constraints,
                             const Weights& weights);
WeightedResult weightedCount(const Instance& inst, const Weights& weights);

// Exact posterior marginal that `cell` is occupied, as a constrained recount
// divided by the total. One sweep per cell, which is why weightedMarginals does
// not use it.
double weightedMarginal(const Instance& inst, const Constraints& constraints,
                        const Weights& weights, int cell);

// Every cell marginal from one forward and one backward pass.
//
// The empty transition maps a state to itself, so the weight that flows through
// a cell without occupying it is
//
//     Z_empty(c) = empty[c] * sum over states s of f_c(s) * b_{c+1}(s)
//
// and the occupied weight is Z - Z_empty(c). One subtraction per cell replaces a
// constrained recount per cell, and the whole board costs two passes instead of
// a hundred: about 8 seconds on the standard instance against 280.
//
// Holding every layer would need roughly 600 MB, so the forward pass keeps only
// the eleven column boundaries and the backward pass replays each column from
// its boundary. That is the same trade the unweighted analyse() makes.
std::vector<double> weightedMarginals(const Instance& inst, const Constraints& constraints,
                                      const Weights& weights);

// The same marginals by the slow route, one constrained sweep per cell. Kept
// because it shares no code with the fast path, so the two agreeing is a real
// check rather than a restatement.
std::vector<double> weightedMarginalsByRecount(const Instance& inst,
                                               const Constraints& constraints,
                                               const Weights& weights);

}  // namespace mayflower
