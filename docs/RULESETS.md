# Other rulesets, and where the approach stops

The printed-puzzle no-touching rule, and the variants that reuse the engine
rather than extend it.

Part of [Mayflower](../README.md).

## Ships that may not touch

The printed-puzzle ruleset is a different counting problem rather than a filter on
this one. Sweeping column-major, the decided 8-neighbours of cell `(r,c)` are
`(r-1,c-1)`, `(r,c-1)`, `(r+1,c-1)` and `(r-1,c)`, and the residual extensions
determine none of them, so the boundary state carries the previous column's
occupancy. It costs `H+1` bits rather than `2H`, because previous and current
column share one `H`-bit word and only `prev[r-1]` needs a carry bit. The standard
instance packs into 49 bits.

Every 8-adjacent pair has one member decided strictly before the other, so
checking a cell against its decided neighbours as it is placed catches every
touching pair, and ship halos need no representation.

| quantity | ships may touch | ships may not touch |
| --- | --- | --- |
| configurations | 15,046,987,768 | 1,925,751,392 |
| entropy | 33.8088 bits | 30.8428 bits |
| lattice edges | 28,743,172 | 18,322,562 |
| peak states | 376,735 | 342,892 |

Forbidding contact removes 87.20% of the space and 36.3% of the lattice, so the
stricter rule counts faster: the adjacency rule kills more profiles than the extra
bits create. The small-board ladder agrees with the independent enumerator on
every case, constrained and unconstrained, and the two share no code.

## Variants

`tools/m9` holds the results that reuse the engine rather than extend it. Full
output in [M9_RESULTS.txt](M9_RESULTS.txt).

**A hider who never commits.** Turning the chance node into a maximum gives a
worst case with no distributional assumption in it. For a lone ship `W*` sits 2 or
3 above `beta(L)`, the margin covering both finishing the ship and guessing its
orientation.

**The DP has no hard region.** Feed both the sweep and a backtracking search
records that no board produced. The search shows the easy-hard-easy profile of
random satisfiability: nodes peak mid-sweep at 39 to 183 times the loose end, and
fall to zero once the record is plainly impossible. The sweep does not. Its cost
is highest where the record is loosest, peaking at 0 hits on all three instances,
and broadly falls as the record tightens, because a counting sweep never
backtracks and a constraint removes work rather than adding it. All 6,720 records
were decided by both engines and agreed on every one.

**What feedback buys.** A non-adaptive player fixes the cell order in advance, so
clearing time depends on each prefix as a set and the best of `n!` orders is the
best chain through the subset lattice, a `2^n` DP. Feedback is worth 2.09x against
a lone ship and 1.44 to 1.50 against a fleet, since feedback buys the right to
skip cells and a fleet covering more of the board leaves fewer worth skipping.
Greedy stays within 3.2% of the optimal order. The 4-approximation quoted for
min-sum set cover does not apply: that objective pays for a set at its first
covered element, this one waits for the last.

At full scale `tools/maxcover` measures the best fixed order at 88.7342 shots for
row-major, which
column-major matches to every digit because the board is square and transposing
is a bijection on configurations. Against a density policy at 44.369, both being
achievable rather than optimal, the pair does not bound the adaptivity gap from
below but does show it is not small.

**Row sums.** Bimaru gives the occupied count of every row and column. A column
sum lives and dies inside its column and costs a factor of `H+1`; a row sum
accumulates across the whole sweep, so all `H` counters ride along. For 10x10 the
half-board cut admits at most 5,044,260 row vectors, which against a peak of
376,735 profile states is 1.9e12 states, past anything the sweep can carry.

**Salvo.** Fire `k` cells, hear how many hit but not which. A turn answering `h`
of `k` splits the record `C(k,h)` ways and the belief becomes a union of
constraint sets, each needing its own sweep. `k=2` costs 2.6x and survives. Real
salvo opens at one shot per surviving ship, so it starts at `k=5` and 56x.

**Noise.** Flip every answer with probability `eps`. A board's likelihood is
`(1-eps)^(t-m) eps^m` in its mismatch count, so the posterior is Boltzmann in that
count and noise is a temperature, with the truthful game as the zero-temperature
limit. Each shot is one use of a channel of capacity `1 - H(eps)`, and measured
cost sits 2.5 to 4.3 times above that floor across both instances measured, with
the ratio flattening once noise dominates.
