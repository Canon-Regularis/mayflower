# Mayflower

[![CI](https://github.com/Canon-Regularis/mayflower/actions/workflows/ci.yml/badge.svg)](https://github.com/Canon-Regularis/mayflower/actions/workflows/ci.yml)

Exact Bayesian inference and optimisation, with Battleships as the problem
instance.

Mayflower maintains the exact posterior over every fleet configuration consistent
with an observation record. A broken-profile transfer-matrix DP replaces
enumeration of 15,046,987,768 configurations with one sweep over a lattice of
2.87e7 edges. On that posterior it compares shot-selection objectives against
certified lower bounds.

The result it exists to measure: identifying the board costs 33.81 bits, which an
ordinary game supplies several times over, yet all 17 ship cells must still be
hit. The entropy bound of 13.08 shots falls below the coverage bound of 17, so
coverage is what binds, and playing for information is measurably worse than
playing for coverage.

## Build

C++20, CMake >= 3.24, Ninja. Developed against MinGW-w64 GCC 13.2 on Windows.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

ctest --test-dir build -L fast     # ~35 s
ctest --test-dir build -L pr       # adds the full 10x10 runs, tens of minutes
```

| | |
| --- | --- |
| `./build/omega0` | Hypothesis-space size and lattice statistics, both rulesets |
| `./build/marginals` | Exact prior heatmap and the 15 dihedral orbit integers |
| `./build/sample` | Uniform board generation by unranking |
| `./build/bounds` | The lower-bound ladder, each rung labelled by how it is established |
| `./build/optimal` | Exact optimal play and the price of each objective |
| `./build/selfplay` | Paired self-play over a seeded pool |
| `./build/weighted` | Opponent priors and noisy observation channels |
| `./build/spectrum` | The sweep as a hard-rod transfer matrix |
| `./build/m9` | Adversary, constraint density, adaptivity, Bimaru, salvo, noise |
| `./build/dp_bench` | The optimisation ladder, pinned and ABBA |

## Continuous integration

Every push and pull request builds on Linux with GCC 13 and Clang, in Release and
Debug, and on Windows with MinGW-w64 on the UCRT runtime, which is the only
configuration that compiles `src/platform/bench_platform_win.cpp`. Each runs the
fast suite. The Linux legs build with `-Werror`; the Windows compiler floats with
the MSYS2 mirror, so warnings there are printed rather than fatal.

Two sanitizers run that the development machine cannot, since MinGW ships no
sanitizer runtimes: ASan with UBSan over the fast suite, and TSan over the
threaded rungs, which is the only concurrency in the engine. TSan needs
`vm.mmap_rnd_bits=28`, or its shadow mapping does not survive the kernel's
default ASLR entropy.

The extended suite (`-L pr`) gates a pull request rather than every push. Nightly
adds board-generator uniformity at 300,000 draws, and rebuilds the figure data
and the report end to end, which is where the two tests that read
`out/figures.json` run against real data instead of reporting Skipped.

Two things are asserted that a green tick would otherwise hide. Five fast tests
are registered only when CMake finds Python or Node, so CI names them and fails
if any is missing rather than passing a suite that quietly shrank. And `ctest`
exits zero on a selection that matches nothing, so every invocation carries
`--no-tests=error`.

```sh
ctest --test-dir build -L fast     # ~35 s, what CI runs on every push
```

## Hypothesis space

```text
Placements per length on 10x10 (2N(N-L+1)):  L=5:120  L=4:140  L=3:160  L=2:180
Distinct placement masks                     600

|Omega_0|  (10x10, {5,4,3,3,2}, ships may touch)     15,046,987,768
  same count with the two 3-ships labelled           30,093,975,536
  H(Omega_0)                                         33.8088 bits
  peak live DP states                                376,735
  edges relaxed in one full pass                     28,743,172
  largest accumulator 17*|Omega_0|                   255,798,792,056  (37.90 bits)
```

Exact prior occupancy marginals, all 100 cells from one forward and one backward
sweep:

```text
        0      1      2      3      4      5      6      7      8      9
r0  0.0800 0.1149 0.1435 0.1587 0.1667 0.1667 0.1587 0.1435 0.1149 0.0800
r1  0.1149 0.1426 0.1655 0.1777 0.1842 0.1842 0.1777 0.1655 0.1426 0.1149
r2  0.1435 0.1655 0.1841 0.1941 0.1994 0.1994 0.1941 0.1841 0.1655 0.1435
r3  0.1587 0.1777 0.1941 0.2034 0.2084 0.2084 0.2034 0.1941 0.1777 0.1587
r4  0.1667 0.1842 0.1994 0.2084 0.2136 0.2136 0.2084 0.1994 0.1842 0.1667
   (rows 5-9 mirror rows 4-0)
```

Corner 0.0800, centre 0.2136, ratio 2.670, mean exactly 0.17. The 15 dihedral
orbit representatives are exact integers summing with their orbits to
`17 * |Omega_0|`; `tools/marginals` prints them.

Board-size scaling of the same fleet, by the same DP:

| board | 6x6 | 7x7 | 8x8 | 9x9 | 10x10 | 11x11 |
| --- | --- | --- | --- | --- | --- | --- |
| \|Omega\| | 3,343,568 | 62,378,548 | 571,126,760 | 3,394,196,128 | 15,046,987,768 | 54,083,238,912 |

Boards are drawn by unranking, at 19,767 per second after a 20 s build. Over
200,000 draws the largest per-cell deviation from the exact marginal is 0.0022,
which is sampling noise at that size. A biased generator would poison every
statistic below, so this is a gate rather than a diagnostic.

## Bounds

```text
E1  coverage        17.0000 shots   all 17 ship cells must be shot
E2  entropy         13.0790 shots   33.8088 bits over an outcome alphabet of 6
E4  water-filling   24.0876 shots   by transcript counting
```

E2 falls below E1, so the entropy bound is vacuous and counting ship cells is the
stronger constraint.

Water filling is the strongest rung established here. Against a deterministic
policy the map from configuration to transcript is injective, since the
transcript replays the policy and so reveals which cells were shot and which were
hits. A game ending on shot `t` has 17 hits with the last at `t`, so its
transcript is fixed by choosing the positions of the other 16 among the first
`t-1` and by one of `K` announcement strings:

```text
P(T <= t) <= K * C(t,17) / N        E[T] >= sum_t max(0, 1 - K*C(t,17)/N)
```

`K = 28,560`, computed by determinising the automaton over per-ship hit counts,
since several hit-to-ship assignments collapse to one announcement string and
counting interleavings would over-count. The sum saturates at depth 25.

Blocking numbers, the fewest shots guaranteeing contact with a lone length-L
ship, exact by a row-sweep DP:

```text
beta(2) = 50    beta(3) = 33    beta(4) = 24    beta(5) = 20
```

A 20-cell blocking set for the 5-ship, fed to the counting DP as misses, drives
the hypothesis space to exactly zero, and restoring any one of those cells revives
it. That establishes that some 20-cell set meets every configuration. It does not
establish that no 19-cell set does, so the adversarial bound
`17 + beta(5) - 1 = 36` that would follow from the other direction is not claimed.
`tools/m9 adversary` computes the worst case exactly on instances small enough to
solve outright.

The obvious way to tighten the rung does not work. Replacing `C(t,17)` with
`maxcov(t)`, the largest number of configurations fitting inside any `t`-cell set,
substitutes a count of configurations for a count of cell subsets, and recovering
a bound that way needs the number of distinct shot-sets a policy can reach by time
`t`, which is the number of length-`t` transcripts and vastly exceeds `K`.
Computed anyway it exceeds the true optimum on every instance small enough to
check. What it does obey there is the **non-adaptive** optimum, and an adaptive
searcher has a tree of shot-sets where the non-adaptive one has a single set.
`tools/maxcover selftest` holds this as a negative regression.

Unresolved interval `[24.088, 44.369]`, a gap of 20.3 shots. Water filling closes
25.9% of the distance from the coverage bound to the best measured policy.

## Optimal play

Where the configuration space is enumerable, the optimum and each policy's
expectation are both exact, so the optimality gap is measured rather than
estimated.

```text
instance       cfgs   optimal   density     gap    parity     gap
3x3 {2}          12    4.5000    4.5000   0.000    5.0000   0.500
4x3 {2}          17    5.1176    5.1176   0.000    5.6471   0.529
4x4 {2}          24    6.0833    6.0833   0.000    6.6250   0.542
4x4 {3}          16    5.6250    5.7500   0.125    6.7500   1.125
5x4 {3}          22    6.2273    6.2273   0.000    8.1364   1.909
4x4 {2,2}       224    8.6696    8.7232   0.054    9.4196   0.750
4x4 {3,2}       264    8.7538    8.8902   0.136    9.7955   1.042
```

The density heuristic is exactly optimal on four of seven instances and within
0.14 shots on the rest. The optimal first shot is an off-corner cell in every
case.

The same machinery prices the three shot-selection objectives. Totals are integers
over the enumerated space, so these gaps carry no sampling error.

```text
instance      cfgs    optimal   max-P(hit)     gap     max-info      gap
4x4 {3,2}      264   8.753788     8.909091  0.1553    12.662879   3.9091
4x4 {2,2}      224   8.669643     8.741071  0.0714    11.401786   2.7321
4x4 {3}         16   5.625000     5.750000  0.1250    10.500000   4.8750
4x3 {2}         17   5.117647     5.117647  0.0000     6.588235   1.4706
3x3 {2}         12   4.500000     4.500000  0.0000     5.500000   1.0000
5x4 {3}         22   6.227273     6.227273  0.0000    13.090909   6.8636
```

Maximising hit probability is exactly optimal on three of six and never worse
than 0.16 shots, so it is a strong heuristic and provably not the optimum.
Maximising one-step information gain is worse by whole shots: on 5x4 {3} it takes
13.09 against an optimum of 6.23.

The belief MDP's memo key is a shot mask plus the surviving support, which is
already the sufficient statistic, so no order-aware transposition table is needed.
Pruning is star1's chance-node bound with move ordering by descending hit
probability, worth a factor of 158 on the fleet instances.

## Self-play

One seeded pool of uniform boards, 20,000 games, every policy on the same boards.

```text
policy                   mean      sd     95% CI on mean  median    p95   best  worst
random                 95.354   4.800  [ 95.288,  95.421]      97    100     57    100
parity-hunt-target     51.535   8.710  [ 51.414,  51.656]      53     64     21     71
density                44.369   8.868  [ 44.246,  44.491]      44     61     20     85
```

The random shooter is the harness self-test: shooting uniformly, the game ends on
the last of the 17 ship cells, so `E[T] = k(N+1)/(k+1) = 95.3889`. Measured
95.3544, inside the interval.

Correlation across the shared pool is bimodal, so sample sizes have to be derived
per comparison. Within the density family it is 0.923, worth 12.9 times fewer
games for the same precision. Against the stochastic hunt policy it is zero,
because that policy's variance comes from its own draws rather than from board
difficulty, so pairing buys nothing there.

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
bits create. The small-board ladder agrees across four implementations sharing no
code.

## Soft evidence and a non-uniform prior

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

Full output in [docs/OPPONENT.txt](docs/OPPONENT.txt) and
[docs/WEIGHTED_MARGINALS.txt](docs/WEIGHTED_MARGINALS.txt).

## Variants

`tools/m9` holds the results that reuse the engine rather than extend it. Full
output in [docs/M9_RESULTS.txt](docs/M9_RESULTS.txt).

**A hider who never commits.** Turning the chance node into a maximum gives a
worst case with no distributional assumption in it. For a lone ship `W*` sits 2 or
3 above `beta(L)`, the margin covering both finishing the ship and guessing its
orientation.

**The DP has no hard region.** Feed both the sweep and a backtracking search
records that no board produced. The search shows the easy-hard-easy profile of
random satisfiability, peaking at roughly 180 times either end. The sweep does
not: its cost falls monotonically with constraint density, because a counting
sweep never backtracks and the record only shrinks a lattice it already paid for.
All 6,720 records were decided by both engines and agreed on every one.

**What feedback buys.** A non-adaptive player fixes the cell order in advance, so
clearing time depends on each prefix as a set and the best of `n!` orders is the
best chain through the subset lattice, a `2^n` DP. Feedback is worth 2.09x against
a lone ship and 1.44 to 1.50 against a fleet, since feedback buys the right to
skip cells and a fleet covering more of the board leaves fewer worth skipping.
Greedy stays within 3.2% of the optimal order. The 4-approximation quoted for
min-sum set cover does not apply: that objective pays for a set at its first
covered element, this one waits for the last.

At full scale the best fixed order measured is row-major at 88.7342 shots, which
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
cost sits 2.8 to 4.3 times above that floor, with the ratio flattening once noise
dominates.

## The transfer matrix

Drop the fleet counter, give each rod a fugacity, and the column operator becomes
the same at every column. Battleships is the fixed-fleet corner of a hard-rod
lattice gas. `lambda_max` comes from power iteration where applying the operator is
one column sweep of the same DP, so no matrix is ever formed; the subdominant
eigenvalue falls out of the convergence rate at a lag of two, because for these
strips it is negative and the correction alternates sign.

Three checks the code was not given:

```text
1-row dimer strip counts Fibonacci      1.618033988750  against the golden ratio
eigenvalue against a finite patch       3.7545140595    against Z(49)/Z(48)
entropy per site, extrapolated to 2D    0.6627990       against 0.6627989727
```

The last is the monomer-dimer entropy of the square lattice, reached from strip
widths 2 to 12 with no input beyond the sweep. The Battleship instance sits at
0.2343 nats per site, well below the free gas, because fixing the rod count turns
a thermodynamic problem into a counting one.

## Bond dimension

The sweep is a matrix-product contraction, so the number of distinct boundary
states is its bond dimension. Cutting between two columns and building the
compatibility matrix `M[l][r]` gives the Schmidt rank, a floor for any linear
representation; the count of distinct rows of `M`, which is the Myhill-Nerode
count and the true floor for a state-based sweep; and what the engine carries.

The engine's state runs 1.86 to 2.80 times larger than the Nerode minimum across
every cut and instance tested, with no sign of the ratio growing. The
representation is sufficient and not minimal, and there is roughly a factor of two
of algorithmic headroom that no amount of cache tuning would reach. What stops the
engine claiming the minimum is that its state is a sufficient statistic computable
from the cells already swept, while a Nerode class is defined by the completions
that follow. Making the second computable locally is open.

## Optimisation ladder

V0 is the original DP, frozen as the reference. V1 packs the state into one uint64
and tags slot liveness with an epoch, so one 64-bit compare settles both liveness
and key equality and clearing a layer is an increment, then stages successors so
their probes prefetch and overlap. V2 replaces each cell with a radix-partitioned
scatter into 64 buckets and a per-bucket merge sized to fit L2. V3 spreads that
merge over a persistent thread pool.

A table-size sweep puts the floor at about 76 ns/edge at 16 MB, rising at 8 MB
from probe chains at load factor 0.72 and at 64 MB from address translation. The
DP is memory-latency bound, so the lever is memory-level parallelism and a faster
hash is beside the point. V1 and V2 attack that bottleneck from opposite sides and
land in the same place.

```text
V0  baseline map, 12-byte key struct           1.00x
V1  packed key, epoch tagging, prefetch        1.67x
V2  radix-partitioned scatter and merge        1.69x     (1.01x against V1)
V3  merge across 8 threads                     2.66x     (turns over at 10)
noise floor, A/A control                       1.02x
```

Every rung is bit-identical to V0 across 965 checks covering the small-board
ladder, the pinned order-dependence cases, 180 fuzzed ordered histories and thread
counts of 1, 2, 4 and 7. The decomposition guarantees it: counts are integers,
integer addition is associative, and the buckets partition the destination keys so
no two merges touch one counter.

**Measurement protocol.** A run pins to one logical processor of a stated class,
warms up, interleaves the rungs ABBA, and runs an A/A control that measures the
noise floor by comparing a rung against itself. The headline is the ratio of
minima: the computation is deterministic, so every deviation above the fastest run
is interference. A speedup smaller than the measured noise floor is refused rather
than reported. Absolute throughput on this machine is not trustworthy, since the
same workload has been observed between 2.45 s and 204 s and pinning did not
remove the spread; ratios between rungs reproduce, absolute ns/edge figures do
not.

## Report

```sh
./build/report_data 20000 > out/figures.json
python tools/render_report.py out/figures.json out/report.html
```

The engine writes data and the renderer reads it. Nothing downstream recomputes an
engine number, so a figure cannot disagree with the engine that produced it, and
the JSON carries its own check: the occupancy counts sum to `17 * |Omega|`
exactly.

Volume is tiered. Anything measured over many games ships as an aggregate, 200
numbers per policy for any number of games. Full per-shot traces exist only for
the three showcase games that need one.

Every figure is complete inline SVG emitted at build time, so the page is correct
with JavaScript disabled and JavaScript only adds hover readouts. Colours come
from a palette validated for colour-vision deficiency in both themes, with the
sequential ramp re-stepped for the dark surface rather than inverted.

Twelve figures, three of which draw the structures the engine runs on: the 15
dihedral orbits, which fold a per-cell computation from 100 evaluations to 15; the
blocking sets, drawn at minimum size as the covering that makes `beta(L)` a
measured number; and the order-dependence counterexample, two boards carrying the
same seven shots with the same seven outcomes in different orders, leaving 41 and
53 configurations standing.

**The live engine.** `web/engine.js` is the same DP in JavaScript, reproducing the
C++ exactly. It is too slow to drive the opening on its own, taking about 27 s at
turn 0 and under 1 s from shot 14, so the widget runs two regimes. While the
posterior spans billions of boards it filters a uniform sample of 200,000
configurations, drawn by the verified unranker and shipped as five bytes per
board. Once fewer than 400 survive the exact sweep takes over. The handoff keys on
the survivor count alone, which makes the two regimes complementary: the sample
runs out only once the record is constraining, and a constraining record is a
cheap sweep. Verified headlessly over ten games: they finish in 29 to 65 shots,
mean 45.0 against the C++ density policy's 44.37, and the true board survives in
the posterior at every turn of every game.

**The belief scrubber** replays one recorded game a turn at a time, showing the
exact posterior on the board. Frames are precomputed and quantised to a byte per
cell, so scrubbing is a lookup. Colour limits are fixed across the whole game,
since a per-frame rescale would hide the collapse the figure exists to show, and
shot cells carry the glyph rather than the colour so the posterior and the record
never compete for one channel.

## Correctness

The DP agrees exactly with brute-force enumeration on nine reduced instances,
including repeated ship lengths and non-square boards:

```text
4x4 {3,2}       264        6x6 {3,3,2}      40,324      6x6 {4,3,3,2}  633,432
5x5 {3,2,2}  12,798        6x6 {4,3,2}      53,624      4x6 {3,2}          840
5x5 {4,3,2}   9,024        5x5 {3,3,2,2}    80,688      7x5 {4,3,2}     46,226
```

Sunk semantics are checked against the oracle's ordered simulator on 300 random
histories. Marginals from `occupancyMap` are checked cell by cell against
constrained counting. `unrank` is checked exhaustively on four instances: it
enumerates the configuration set exactly once per rank, which proves uniformity
outright. Degenerate fleets are checked against arithmetic rather than against
another sweep, since `k` indistinguishable single cells on `n` free cells is
`C(n,k)`. The JavaScript engine is checked against `python/oracle.py`, which
shares no code with it or the C++. The invariant
`sum over cells of P(cell occupied) = shipCells` holds in exact integers
throughout.

Two things to know before changing the engine:

**Indistinguishable ships need no correction.** The fleet counter records how many
ships of each length have been started, never which, so the DP counts unordered
physical boards. There is no division by `2!`.

**The posterior depends on shot order.** `SUNK(x,L)` means the shot at `x` sank the
ship, so the rest of it was already hit. A predicate requiring only
`cells(ship) subset-of HIT` over-counts, 26 against a true 22 on a reproduced 5x5
case, and two orderings of one shot multiset give 41 and 53. Memo keys must be
order-aware. See [docs/ORDER_DEPENDENCE.md](docs/ORDER_DEPENDENCE.md).

## Experimental discipline

Every board id belongs to exactly one of TRAIN, VAL or TEST, decided by hashing
the id and thresholding 60 / 20 / 20, so a board keeps its fold forever.
Thresholding rather than a modulus is deliberate: moving a boundary later moves
only the boards that boundary crosses, where a modulus would reshuffle the whole
space. The rule lives in `include/mayflower/folds.hpp` and `python/stats.py`, and
both assert the same pinned vector, so drift fails the build on either side.
`tools/selfplay` defaults to TRAIN and refuses TEST.

TEST is sealed. Reading it requires an unseal entry in
[`experiments/audit.log`](experiments/audit.log) recorded before the number is
read, and `audit.log.head` records how many entries there should be and what the
last hash is, which catches an edit, an interior deletion, a truncated tail, and
an entry commented out so it stays visible while leaving the chain. It is not
tamper-proof: the digest takes only public inputs, so anyone with write access can
recompute the history. What the chain buys is that tampering costs a rewrite of
the log, the head and the version history containing them, rather than an edit to
one line. Git is the anchor.

Every interval `stats.py` produces is calibrated by simulating from a known ground
truth and counting how often the interval covers it. Coverage of a binomial
interval is a finite sum, so it is computed rather than sampled. Wilson lands
closer to 95% than the normal approximation at every `p` at or below 0.10, by a
factor of fifty at `p = 0.01`, and the two coincide at `p = 0.5`.

Sample sizes, both columns paired designs, at alpha 0.05 and power 0.80:

| effect | rho = 0.923 | rho = 0.00 |
| --- | --- | --- |
| 0.10 shots | 9,510 | 123,506 |
| 0.25 shots | 1,522 | 19,761 |
| 0.50 shots | 381 | 4,941 |
| 1.00 shots | 96 | 1,236 |

At rho = 0 pairing buys nothing and the requirement equals the independent
per-arm figure, so the second column serves as both. Because the correlation is
bimodal, the same comparison against two opponents differs by a factor of
thirteen and no single number covers both. The formula is checked by simulating at
exactly the prescribed `n` and confirming the promised 80% power arrives, measured
at 0.807.

## Known limitations

- The `Sampler` holds backward counts for every layer, about 397 MB on the
  standard instance. Batch generation would remove that; it is not needed until
  board banks get large.
- The no-touching sweep packs its state into one uint64, so it stops at about 13
  rows. `noTouchSupports()` reports whether an instance fits.
- The report page is about 1.5 MB, over the 0.7 to 1.0 MB budget. Nearly all of it
  is the base64 board pool the live widget needs.
- The weighted forward-backward does not rescale, because the backward pass has to
  combine `f` and `b` from the same layer and a per-layer scale would not cancel
  the way a global one does. Weights extreme enough to leave a double need
  `weightedMarginalsByRecount`, which divides two equally scaled counts; the
  forward-backward detects the case and throws rather than returning marginals
  that are inside [0, 1] and wrong.
- The belief MDP caps near 300 configurations, which stops the adaptive column of
  the adaptivity table well before the subset lattice runs out.
- The `pr` suite does not complete reliably on the development machine. Two
  consecutive runs each hit one 900 s timeout, on a different test each time, and
  both of those tests finish in 34 and 48 seconds when run alone. The cause is
  background load rather than a tight limit, and it is the same interference that
  makes absolute timings here untrustworthy.

## Layout

```text
include/mayflower/   constants.hpp (single source of truth), board128, instance,
                     observations, profile_dp, notouch, weighted, folds
src/core/            the DP and its rungs, marginals, sampler, no-touching,
                     weighted counting
src/certify/         blocking numbers, announcement transcripts
src/search/          the belief MDP and its pruning
src/lattice/         the transfer matrix
src/platform/        core topology and pinning, for the benchmarks
tools/               omega0, marginals, sample, selfplay, bounds, optimal,
                     spectrum, m9, weighted, maxcover, opponent, report_data
bench/               the ladder under the measurement protocol
web/                 the JS engine, the live widget, the belief scrubber
tests/oracle/        independent brute-force enumerator and ordered simulator
python/              order-aware reference model, bond dimension, the analysis
                     layer and its calibration
experiments/         pre-registration, registry, append-only audit log
docs/                correctness hazards, complexity notes, captured results
```

## Licence

Apache-2.0. See [LICENSE](LICENSE.md).
