# Weighted counting: soft evidence and a non-uniform prior

The sweep as a partition function, the three bridges that validate it, and where
its floating point stops being exact.

Part of [Mayflower](../README.md).

Weighting the sweep turns the count into a partition function. Two mechanisms
compose multiplicatively:

- **per placement**, applied when a ship starts. A log-linear opponent prior is a
  set of these weights.
- **per cell**, applied by whether the cell ends up occupied or empty. An
  observation channel lives here, so one sweep returns the exact normaliser for a
  noisy record.

Integer exactness is gone, so the path carries three bridges. Every weight at 1
returns 15,046,987,768 bit for bit. The log-linear prior at `theta = 0` reproduces
the exact marginal table the integer path derives. At `eps = 0.5` every board has
likelihood `2^-t` whatever it looks like, so the evidence must read exactly -20.00
bits at 20 shots and -40.00 at 40, and it does, which prices the column at full
10x10 scale without reference to any small board.

Weighted results agree with an independently written enumerator to within a few
ULP across six cases covering each mechanism alone and both together, at a stated
tolerance of 1e-12 relative; only the unit-weight cases are bit-exact.

Intermediate layers are not bounded by the final count, since a layer counts
partial placements and a hard-constrained board can answer in the hundreds of
thousands while its layers still carry billions. The honest bound is the number of
partial placements, about 8.0e10 against 2^53, and it is instance-dependent, so
every run reports its measured `maxLayerSum`; the standard instance measures
1.583e10. `WeightedResult` carries an `exact` flag, true only when the weights
were all 1, no layer sum reached that limit, no rescale intervened and nothing
underflowed.

Against an opponent who hugs the edges, `tools/opponent` prices the model exactly:
every board is enumerated and weighted by how often the opponent produces it, so
the only sampling is in the learning. On 5x5 {4,3,2} against a strong edge-hugger
the oracle gain is 1.14 shots, and fitting the single parameter captures all of it
in about ten games. Estimating placement frequencies with no parametric form costs
forty to a hundred times more games for the same gain. Believing a strong bias
against an opponent who has none costs 0.51 shots, but a mild permanent assumption
dominates the uniform one in the worst case, by 0.14 shots on 5x5 {4,3,2}. All of
it is small against the 20.3-shot gap between the bound and the best policy, which
is why opponent modelling stays out of the headline engine.

Full output in [OPPONENT.txt](OPPONENT.txt) and
[WEIGHTED_MARGINALS.txt](WEIGHTED_MARGINALS.txt).
