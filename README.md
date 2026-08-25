# Mayflower

Exact Bayesian inference and optimisation engine, using Battleships as the
problem instance.

Mayflower maintains the exact posterior over every fleet configuration
consistent with the observation record. A broken-profile transfer-matrix DP
replaces enumeration of 15,046,987,768 configurations with a single sweep over a
lattice of 2.87e7 edges. On that posterior it compares shot-selection objectives
(hit probability, information gain, expected remaining shots) against certified
lower bounds and localises the residual optimality gap.

Identifying the board takes 33.81 bits, and a typical game carries far more
information capacity than that, but all 17 ship cells must be hit. The entropy
bound of 13.08 shots therefore falls below the trivial coverage bound of 17, and
coverage is what binds.

## Status

Milestones M0 to M4 are complete apart from the max-coverage bound, both rulesets
included. M5 has exact
optimal play on small instances, M6 has its first ladder rung and measurement
platform, M7 and M8 have a working report pipeline, and M9 is complete: the
transfer-matrix spectrum, the bond-dimension question, the adaptive adversary,
constraint density, the adaptivity gap, complexity and related work, noise, and
the Bimaru and salvo breakdowns.

| component | |
| --- | --- |
| `src/core/profile_dp.cpp` | The DP, parameterised over board size and fleet |
| `src/core/notouch.cpp` | The same sweep under the rule that ships may not touch |
| `src/core/weighted.cpp` | The sweep as a partition function: opponent priors and noisy answers |
| `tools/weighted` | Evidence, posterior heatmaps and a log-linear opponent model |
| `tools/maxcover` | Why the max-coverage rung is not a rung |
| `include/mayflower/observations.hpp` | Ordered observation record and the placement predicate it induces |
| `occupancyMap` | Every cell marginal from one forward and one backward sweep |
| `Sampler` | Exact uniform sampling by unranking; the board generator |
| `tools/omega0` | Reproduces \|Omega_0\| and the lattice statistics |
| `tools/marginals` | The exact prior heatmap and the 15 D4 orbit integers |
| `tools/sample` | Uniform board generation, checked against the exact marginals |
| `tools/selfplay` | Paired self-play over a seeded pool, with correlations and confidence intervals |
| `tools/bounds` | The lower-bound ladder, each rung labelled by how firmly it is established |
| `src/certify/blocking.cpp` | Exact blocking numbers by a row-sweep DP |
| `src/certify/transcripts.cpp` | Announcement-string counting and the water-filling bound |
| `src/lattice/spectrum.cpp` | The transfer matrix diagonalised: free energy, density, correlation length |
| `tools/spectrum` | The hard-rod strip as a lattice gas |
| `python/bond_dimension.py` | How small the boundary state could be |
| `docs/COMPLEXITY.md` | Which step makes this hard, with the references checked |
| `tools/m9` | Adversary, constraint density, adaptivity, Bimaru, salvo, noise |
| `src/search/exact_solver.cpp` | Exact optimal play on small instances |
| `tools/optimal` | Optimal play and the measured optimality gap of each objective |
| `src/core/profile_dp_fast.cpp` | Ladder rung V1: packed key, epoch tagging, batched prefetch |
| `src/platform/` | Core-topology detection and thread pinning for benchmarking |
| `bench/dp_bench` | The ladder under the measurement protocol |
| `tools/report_data` | Runs the analyses and writes the figure-data contract as JSON |
| `tools/render_report.py` | Renders that JSON as a self-contained HTML report |
| `tools/export_pool` | Uniform board sample for the browser engine, five bytes per board |
| `web/engine.js` | The DP ported to JavaScript, exact and verified against the C++ |
| `web/live.js` | The live widget: sampled posterior early, exact sweep late |
| `outcomeDistribution` | Exact one-ply {MISS, HIT, SUNK(L)} split and information gain per cell |
| `include/mayflower/policy.hpp` | Random, parity hunt/target, density (cheap tier), and exact-posterior policies |
| `tests/oracle/` | Independent brute-force enumerator and ordered simulator |
| `python/oracle.py` | Order-aware reference model |

Still to come: expectimax with pruning and the order-aware transposition table,
the parallel and cache-blocked ladder rungs, the belief scrubber in the report,
plus the unlearnability audit that M10 needs. The max-coverage rung was
investigated and withdrawn.

## Build

C++20, CMake >= 3.24, Ninja. Developed against MinGW-w64 GCC 13.2 on Windows.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

ctest --test-dir build -L fast     # 3 suites, ~2 s
ctest --test-dir build -L pr       # adds the full 10x10 runs, ~2.5 min

./build/omega0                     # the hypothesis-space size
./build/marginals                  # the exact prior heatmap
./build/bounds                     # the lower-bound ladder
./build/optimal                    # optimal play and the price of each objective
./build/dp_bench                   # the optimisation ladder, pinned and ABBA
python python/oracle.py --order-dependence

./build/report_data 20000 > out/figures.json
python tools/render_report.py out/figures.json out/report.html
```

## Results

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

Exact prior occupancy marginals, all 100 cells in one forward and one backward
sweep (16.7 s), against 252 s for the same heatmap by repeated constrained
counting:

```text
        0      1      2      3      4      5      6      7      8      9
r0  0.0800 0.1149 0.1435 0.1587 0.1667 0.1667 0.1587 0.1435 0.1149 0.0800
r1  0.1149 0.1426 0.1655 0.1777 0.1842 0.1842 0.1777 0.1655 0.1426 0.1149
r2  0.1435 0.1655 0.1841 0.1941 0.1994 0.1994 0.1941 0.1841 0.1655 0.1435
r3  0.1587 0.1777 0.1941 0.2034 0.2084 0.2084 0.2034 0.1941 0.1777 0.1587
r4  0.1667 0.1842 0.1994 0.2084 0.2136 0.2136 0.2084 0.1994 0.1842 0.1667
   (rows 5-9 mirror rows 4-0)
```

The 15 D4 orbit representatives as exact integers, which sum with their orbits
to `17 * |Omega_0| = 255,798,792,056`:

```text
(0,0) 1,203,741,932   (1,1) 2,146,161,986   (2,2) 2,769,717,852   (3,3) 3,060,754,006
(0,1) 1,729,219,394   (1,2) 2,490,360,157   (2,3) 2,920,534,022   (3,4) 3,136,128,540
(0,2) 2,158,897,172   (1,3) 2,674,167,548   (2,4) 3,000,859,368   (4,4) 3,214,027,020
(0,3) 2,387,278,606   (1,4) 2,771,697,341
(0,4) 2,508,505,461
```

Corner 0.0800, centre 0.2136, ratio 2.670, mean exactly 0.17.

Board-size scaling of the same fleet, all by the same DP:

| board | \|Omega\| |
| --- | --- |
| 6x6 | 3,343,568 |
| 7x7 | 62,378,548 |
| 8x8 | 571,126,760 |
| 9x9 | 3,394,196,128 |
| 10x10 | 15,046,987,768 |
| 11x11 | 54,083,238,912 |

Uniform board generation draws 19,767 boards/s after a 20 s build. Over 200,000
draws the largest per-cell deviation from the exact marginal is 0.0022, which is
sampling noise at that sample size.

## Bounds

```text
E1  coverage        17.0000 shots   exact: all 17 ship cells must be shot
E2  entropy         13.0800 shots   exact: 33.8088 bits over an outcome alphabet of 6
E4  water-filling   24.0876 shots   exact, by transcript counting
```

E2 falls below E1, so counting ship cells is the stronger constraint and the
entropy bound is vacuous. Identifying the board is cheap; hitting all of it is
what costs.

The water-filling rung is the binding one. Against a deterministic policy the map
from configuration to transcript is injective, since the transcript replays the
policy and so reveals which cells were shot and which of those were hits. A game
ending on shot `t` has 17 hits with the last at `t`, so its transcript is fixed by
choosing the positions of the other 16 among the first `t-1` and by one of `K`
announcement strings. Hockey-stick then gives

```text
P(T <= t) <= K * C(t,17) / N        E[T] >= sum_t max(0, 1 - K*C(t,17)/N)
```

`K = 28,560`, computed by determinising the automaton over per-ship hit counts
(several hit-to-ship assignments collapse to one announcement string, so counting
interleavings would over-count) and validated against brute force on eight
fleets. The sum saturates at depth 25 and evaluates to 24.0876.

Blocking numbers, the fewest shots guaranteeing contact with a lone length-L
ship, computed exactly by a row-sweep DP and checked against brute force on
eleven small boards:

```text
beta(2) = 50    largest 2-free set 50     (the checkerboard)
beta(3) = 33    largest 3-free set 67
beta(4) = 24    largest 4-free set 76
beta(5) = 20    largest 5-free set 80
```

Greedy set cover reaches beta exactly for L=2 and L=5, and misses for L=3 (34
against 33) and L=4 (26 against 24).

Two independent components cross-check here: a 20-cell blocking set for the
5-ship, fed to the counting DP as misses, drives the hypothesis space to exactly
zero, and restoring any single one of those cells revives it.

That establishes that some 20-cell set meets every configuration. It does not
establish that no 19-cell set does, so the adversarial worst-case bound of
`17 + beta(5) - 1 = 36` that would follow from the other direction is not
claimed. On instances small enough to solve outright, `tools/m9 adversary`
computes the worst case exactly instead of bounding it. The max-coverage rung
is not implemented and is therefore not quoted.

Unresolved interval: `[24.088, 44.369]`, a gap of 20.3 shots. Water-filling
closes 25.9% of the distance from the coverage bound to the best measured policy.

## Exact optimal play on small instances

Where the whole configuration space is enumerable, the optimum and each policy's
expectation are both exact, so the optimality gap is measured and not estimated.

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

The density heuristic is exactly optimal on four of the seven instances and
within 0.14 shots on the rest. Parity hunt/target gives up between 0.5 and 1.9
shots. The optimal first shot is an off-corner cell in every case.

### Which objective, measured exactly

The same machinery prices the three shot-selection objectives against the true
optimum. Totals are integers over the enumerated space, so these gaps are exact
and contain no sampling error.

```text
instance      cfgs    optimal   max-P(hit)     gap     max-info      gap
4x4 {3,2}      264   8.753788     8.909091  0.1553    12.662879   3.9091
4x4 {2,2}      224   8.669643     8.741071  0.0714    11.401786   2.7321
4x4 {3}         16   5.625000     5.750000  0.1250    10.500000   4.8750
4x3 {2}         17   5.117647     5.117647  0.0000     6.588235   1.4706
3x3 {2}         12   4.500000     4.500000  0.0000     5.500000   1.0000
5x4 {3}         22   6.227273     6.227273  0.0000    13.090909   6.8636
```

On 4x4 {3,2} the totals are 2311 shots for optimal play and 2352 for greedy
max-P(hit), a difference of exactly 41 over 264 configurations.

Two results fall out. Maximising hit probability is exactly optimal on three of
six instances and never worse than 0.16 shots, so it is a strong heuristic but
provably not optimal. Maximising one-step information gain is far worse, by whole
shots: on 5x4 {3} it takes 13.09 against an optimum of 6.23, more than double.

That is the coverage-limited thesis, now measured and no longer just argued. Identifying the
board is cheap, and shots spent identifying it instead of covering it are wasted.

The solver's own check is that the optimum can never exceed what a concrete
policy achieves. That invariant caught a real measurement bug: seeding a
stochastic policy from the board's identity gives each board its own policy
randomisation, which measures a family of policies each paired with its own board
and duly scored below the single-policy optimum. Policy seeds are now drawn from
a stream independent of the board pool.

## The transfer matrix

The counting DP carries a fleet counter, which ties it to one fixed fleet and
makes the column operator depend on how much of the fleet is spent. Drop the
counter, give each rod a fugacity, and the operator becomes the same at every
column. That is a transfer matrix, and Battleships turns out to be the
fixed-fleet corner of a hard-rod lattice gas.

`lambda_max` comes from power iteration where applying the operator is one column
sweep of the same DP, so no matrix is ever formed. The subdominant eigenvalue
falls out of the convergence rate, at a lag of two, because for these strips it
is negative and the correction alternates sign.

Three checks, none of which the code was given:

```text
1-row dimer strip counts Fibonacci
  lambda      1.618033988750
  golden      1.618033988750

eigenvalue against a finite patch, k=4 H=4
  lambda      3.7545140595
  Z(49)/Z(48) 3.7545140612

entropy per site extrapolated to two dimensions, f(H) = f - a/H
  extrapolated 0.6627990
  published    0.6627989727      difference 2.4e-10
```

That last one is the monomer-dimer entropy of the square lattice, reached from
strip widths 2 to 12 with no input beyond the sweep itself.

The strip results also separate by rod length: for dimers and trimers the
subdominant eigenvalue is negative, so correlations alternate column to column,
while for 4-mers and 5-mers it is not. Correlation lengths run from 0.6 columns
for dimers to about 4 for 5-mers.

For reference, the Battleship instance is 0.2343 nats per site, well below the
free gas, because fixing the rod count is exactly what turns a thermodynamic
problem into a counting one.

## Is the boundary state minimal?

The sweep is a matrix-product contraction, so the number of distinct boundary
states is its bond dimension. Cutting the board between two columns and building
the compatibility matrix `M[l][r]` over left and right parts gives three numbers
that are easy to confuse:

- `rank(M)`, the Schmidt rank, a floor for any linear representation. Reaching it
  may need negative or fractional weights, which a counting sweep cannot use.
- distinct rows of `M`, the Myhill-Nerode count. Two left parts with identical
  rows admit the same completions, so a counting sweep may merge them and add
  their counts. This is the true floor for a state-based sweep.
- what the engine carries: the residual extension per row plus the fleet counter.

```text
instance         cut   rank(M)   nerode   engine   ratio
5x5 {3,2,2}        1        38       60      113    1.88
                   2        43       76      156    2.05
                   3        36       67      156    2.33
5x5 {4,3,2}        2        71      178      451    2.53
                   3        48      101      258    2.55
4x4 {3,2}          2        13       15       42    2.80
```

The engine's state runs 1.86 to 2.80 times larger than the Nerode minimum across
every cut and instance tested, with no sign of the ratio growing. So the answer is
no: the representation is sufficient and not minimal, and there is roughly a
factor of two of algorithmic headroom that no amount of cache tuning would reach.

What stops the engine claiming it is that its state is a sufficient statistic
computable from the cells already swept, while a Nerode class is defined by the
completions that follow. Turning the second into something computable locally is
open.

A hand-checkable case anchors the method: one length-4 ship on a 4x4 board, cut
after the first column. Every left part that already holds the ship behaves the
same way afterwards, so two classes suffice, and the script reports rank 2,
nerode 2, engine 6.

## Self-play baseline

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

That replaces the 44-to-61 spread of unreconciled baselines with one number and a
stated interval. The gap from the coverage bound to the density policy is 27.4
shots.

Correlation across the shared pool turns out to be bimodal, which changes how
sample sizes must be planned:

```text
                       random  parity-hunt  density(10)  density(50)
random                  1.000       -0.013       -0.012       -0.011
parity-hunt-target     -0.013        1.000       -0.012       -0.012
density(b=10)          -0.012       -0.012        1.000        0.923
density(b=50)          -0.011       -0.012        0.923        1.000
```

Within the density family the correlation is 0.923 and the paired interval is
12.9 times narrower than the unpaired one. Against the stochastic hunt policy the
correlation is zero, because that policy's variance comes from its own draws
and not from board difficulty, so pairing buys nothing there. Sample sizes
have to be derived per comparison from the measured correlation.

A side result: the density policy's hit bonus saturates. Bonus 50 and bonus 200
produce byte-identical play, correlation exactly 1.000.

## Optimisation ladder

V0 is the original DP, frozen as the reference. V1 packs the state into one
uint64 (30 bits of profile, 3 of vertical run, 5 of fleet index), tags slot
liveness with an epoch in the spare high bits so one 64-bit compare settles both
liveness and key equality and clearing a layer is an increment, pre-sizes the
table, and stages successors so their probes prefetch and overlap.

The last change is the one that matters. A table-size sweep puts the floor at
about 76 ns/edge at 16 MB, rising at 8 MB (probe chains at load factor 0.72) and
at 64 MB (address translation), which is one DRAM round trip per edge. The DP is
memory-latency bound, so the lever is memory-level parallelism, with a faster
hash being beside the point.

```text
speedup     1.94x  (ratio of minima)
noise floor 1.39x  (A/A control)
counts and edge totals identical between rungs
```

Measured four times: 1.99, 1.85, 1.88, 1.94. `tests/test_ladder.cpp` holds the
rungs to bit-identical output across 197 checks, including 180 fuzzed ordered
histories and the pinned order-dependence cases.

### Measurement protocol

Topology detection reports the machine as 4 logical processors in efficiency
class 1 (2 P-cores with SMT) and 8 in class 0 (E-cores), which matches the part
independently.

A run pins to one logical processor of a stated class, warms up, interleaves the
rungs ABBA, and runs an A/A control that measures the noise floor by comparing a
rung against itself. The headline is the ratio of minima: the computation is
deterministic, so every deviation above the fastest run is interference, and the
fastest run is the closest estimate of true cost. Medians and the full spread
ship alongside. A speedup smaller than the measured noise floor is refused, never
reported, and the harness has already exercised that refusal.

**Absolute throughput on this machine is not currently trustworthy.** The same
workload has been observed between 2.45 s and 204 s, and pinning did not remove
the spread, so this machine appears to carry persistent background load. Ratios
between rungs reproduce; absolute ns/edge figures do not. An earlier revision of
this file quoted 7.4 M edges/s and 135.7 ns/edge from an unpinned run; that
number is withdrawn, and no absolute figure replaces it until the ladder is
measured on a quiet machine.

## Report

```sh
./build/report_data 20000 > out/figures.json
python tools/render_report.py out/figures.json out/report.html
```

The engine writes data; the renderer reads it. Nothing downstream recomputes an
engine number, so a figure cannot disagree with the engine that produced it. The
JSON carries its own check: the occupancy counts sum to `17 * |Omega|` exactly.

Volume is tiered. Anything measured over many games ships as an aggregate: a
shot-count histogram and a per-cell mean shot turn, 200 numbers per policy for
any number of games. Full per-shot traces exist only for the three showcase games
that need one, because a 100-cell posterior per turn per game across tens of
thousands of games is not storable.

The page is one file of about 105 KB. Every figure is complete inline SVG emitted
at build time, so the report is correct with JavaScript disabled; JavaScript only
adds hover readouts. The only external request is the webfont. Colours come from
a palette validated for colour-vision deficiency in both light and dark themes,
and the sequential ramp is re-stepped for the dark surface instead of inverted.

Twelve figures: the exact prior heatmap, board-size scaling, the lattice layer
profile, the bound ladder, the objective comparison, two shot-order maps,
survival curves, posterior collapse, and three that draw the structures the
engine runs on. The page opens with a live engine.

Those last three exist because a report full of statistics about games never
shows the objects the engine actually manipulates. They are the 15 dihedral
orbits, which fold a per-cell computation from 100 evaluations to 15; the
blocking sets, drawn as the covering that makes beta(L) a measured number and
not an assertion; and the order-dependence counterexample, two boards carrying the same
seven shots with the same seven outcomes in different orders, leaving 41 and 53
configurations standing.

### The live engine

`web/engine.js` is the same broken-profile DP in JavaScript. It reproduces the
C++ exactly: 15,046,987,768 for the prior, occupancy summing to 17.000000, and
the same corner and centre marginals to six places.

It is also far too slow to drive the opening on its own. Measured through a real
game: 30 s at shot 4, 9 s at shot 8, 4 s at shot 10, then under 1 s from shot 14.
The cost tracks the size of the surviving space.

So the widget runs two regimes with opposite cost curves. While the posterior
spans billions of boards it reads a uniform sample of 200,000 configurations,
drawn by the verified unranker and shipped as five bytes per board; filtering the
whole pool against the ordered history costs a few tens of milliseconds. Once
fewer than 400 survive, the sample is spent and the exact sweep takes over, which
is precisely when it has become cheap. The crossover is self-tuning, because the
survivor count is a proxy for the size of the space, and the readout says which
regime is answering.

Verified headlessly over ten games: they finish in 29 to 65 shots, mean 45.0
against the C++ density policy's 44.37, the handoff lands at shot 6 to 12, and
the true board survives in the posterior in every game at every turn.

## Validation

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
outright and leaves nothing to a statistical argument. The invariant
`sum over cells of P(cell occupied) = shipCells` holds in exact integers
throughout.

## Two things to know before contributing

**Indistinguishable ships need no correction.** The fleet counter records how
many ships of each length have been started, never which, so the DP counts
unordered physical boards. There is no division by `2!`.

**The posterior depends on shot order.** `SUNK(x,L)` means the shot at `x` sank
the ship, so the rest of it was already hit. A predicate requiring only
`cells(ship) subset-of HIT` over-counts: 26 against a true 22 on a reproduced 5x5
case, and two orderings of one shot multiset give 41 and 53. Memo keys must be
order-aware. See [docs/ORDER_DEPENDENCE.md](docs/ORDER_DEPENDENCE.md).

## What feedback is worth, and where the engine stops

`tools/m9` holds the results that reuse the engine rather than extend it. Run a
section by name (`adversary`, `density`, `adaptivity`, `bimaru`, `salvo`,
`noisy`) or all of them with no argument. Full output in
[docs/M9_RESULTS.txt](docs/M9_RESULTS.txt).

### A hider who never commits

Expected shots assume the board was fixed before play. Against a hider who
answers each shot to hurt most while staying consistent, the chance node becomes
a maximum and the answer is a worst case with no distributional assumption in it.

```text
instance      boards  E[T] committed  W* adaptive     gap  beta(L)
3x3 {2}           12          4.5000           7     2.50        4
4x3 {2}           17          5.1176           8     2.88        6
4x4 {2}           24          6.0833          10     3.92        8
4x4 {3}           16          5.6250           8     2.38        5
5x4 {3}           22          6.2273           9     2.77        6
4x4 {2,2}        224          8.6696          12     3.33        -
4x4 {3,2}        264          8.7538          12     3.25        -
```

`W*` is an integer, as a worst case over a finite tree must be. For a lone ship
it sits 2 or 3 above `beta(L)`, the shots that guarantee first contact, and the
margin is not a fixed offset because the adversary also picks the orientation.

### The DP has no hard region

Feed both the sweep and a backtracking search records that no board produced,
then vary how constrained they are. The search shows the easy-hard-easy profile
of random satisfiability; the sweep does not.

```text
8x8 {5,4,3,3,2}, 34 cells shot, 40 records per point
  hits  feasible  DP mean us  search nodes
     0     37.5%        4695             3
     4     25.0%        4525           339
     8      5.0%        2057           548
    12      0.0%        1848           188
    20      0.0%        1105             0
```

Search cost peaks in the middle at roughly 180 times either end. DP cost falls
monotonically, because a counting sweep never backtracks: it pays for the whole
lattice up front and the record only shrinks it. The worst record costs less
than the empty one, which is the argument for exact inference in one measurement.

All 6,720 records were decided by both engines and the answers agreed on every
one, which cross-checks the DP against an independent implementation on records
no board generated.

### What feedback buys

A non-adaptive player fixes the cell order in advance. The clearing time then
depends on each prefix as a set, so

```text
E[T] = n - (1/N) sum_t c(S_t)
```

and the best of the `n!` orders is the best chain through the subset lattice, a
`2^n` DP. Both optima are exact.

```text
instance      boards  adaptive  fixed order  greedy order     gap   ratio
3x3 {2}           12    4.5000       5.9167        6.0000  1.4167  1.3148
4x4 {3}           16    5.6250      10.8750       11.0000  5.2500  1.9333
5x4 {3}           22    6.2273      13.0455       13.3636  6.8182  2.0949
4x4 {2,2}        224    8.6696      12.4866       12.5312  3.8170  1.4403
4x4 {3,2}        264    8.7538      13.1098       13.3182  4.3561  1.4976
5x4 {3,2}        510         -      15.8902       16.1765       -       -
5x4 {4,3,2}     2520         -      18.1437       18.5147       -       -
```

Feedback is worth most against a lone ship, 2.09x on 5x4 {3}. Fleets score
lower, 1.44 and 1.50, because what feedback buys is the right to skip cells and a
fleet covering more of the board leaves fewer worth skipping. On 5x4 the
fixed-order cost runs 13.05, 15.89 and 18.14 of 20 cells as the fleet grows.

Greedy stays within 3.2% of the optimal order everywhere here. The 4-approximation
people quote belongs to min-sum set cover, where a set is paid for at its first
covered element; this objective waits for the last one, which is the `K(S) = |S|`
case and carries no such guarantee. See [docs/COMPLEXITY.md](docs/COMPLEXITY.md).

### Where the approach stops

Three ways, all measured rather than asserted.

**Row sums.** Bimaru gives the occupied count of every row and column. Sweeping
column-major, a column sum lives and dies inside its column and multiplies the
state by `H+1`. A row sum accumulates across the whole sweep, so all `H` counters
ride along:

```text
instance        boards   cut  row vectors  column sums
4x4 {3,2}          264     3           82            5
5x5 {4,3,2}       9024     4          882            6
6x6 {4,3,2}      53624     4         2338            7
6x6 {4,3,3,2}   633432     5         8675            7
```

For 10x10 the half-board cut admits at most 5,044,260 row vectors. Times a peak
of 376,735 profile states that is 1.9e12, and the sweep is finished. The two
halves of Bimaru's input split cleanly, and transposing only swaps which half is
free.

**Salvo.** Fire `k` cells, hear how many hit, not which. A turn answering `h` of
`k` splits the record `C(k,h)` ways and the belief becomes a union of constraint
sets, each needing its own sweep.

```text
5x5 {4,3,2}, 9024 boards, 60 games per row
    k    turns     split  peak union  sweeps/game  vs classic
    1     23.6       0.0           1         23.6         1.0
    2     11.5       5.8          19         59.8         2.6
    3      7.9       5.8          63        190.7         8.1
    4      5.9       4.9         124        479.8        20.3
    5      4.8       4.5         260       1363.1        56.2
```

`k=2` costs 2.6x and survives. Real salvo opens at one shot per surviving ship,
so it starts at `k=5` and 56x. The union stays far under the product of the
fan-outs because most assignments contradict the fleet within a turn or two, and
nothing in the profile state merges branches: two of them disagree about cells
the sweep has already passed.

**Noise.** Flip every answer with probability `eps`. A board's likelihood after
`t` shots is `(1-eps)^(t-m) eps^m` in its mismatch count, so

```text
P(B | O)  proportional to  exp(-beta m),   beta = ln((1-eps)/eps)
```

The posterior is Boltzmann in the mismatch count and noise is a temperature, with
the truthful game as the zero-temperature limit. Verified against the likelihood
product to 1.7e-15.

```text
5x5 {4,3,2}, H0 = 13.1396 bits
    eps     beta  capacity  shots used   bound  ratio
 0.0005     7.60    0.9938        36.4    13.2   2.76
 0.0500     2.94    0.7136        67.4    18.4   3.66
 0.1000     2.20    0.5310        98.4    24.7   3.98
 0.3000     0.85    0.1187       472.2   110.7   4.27
```

Each shot is one use of a channel of capacity `1 - H(eps)`, so `H0 / (1 - H(eps))`
is a floor. Measured cost sits 2.8 to 4.3 times above it, and the ratio flattens
once noise dominates: the capacity term captures the noise scaling correctly, and
the leftover constant is the price of shooting at a uniform random cell instead
of an informative one.

Scaling any of this to 10x10 needs a weighted sweep. The counting path carries
exact `uint64` counts throughout and a noisy posterior needs floating-point
weights on the transitions, so it is a second pass rather than a flag. Everything
above is exact by enumeration and stops where enumeration stops.

## The printed-puzzle ruleset

Ships that may not touch, not even at a corner, is a different counting problem
rather than a filter on this one.

The profile the standard sweep carries cannot express it. Sweeping column-major,
the decided 8-neighbours of cell `(r,c)` are

```text
(r-1, c-1)   (r, c-1)   (r+1, c-1)   (r-1, c)
```

and the residual extensions determine none of them: a horizontal ship ending at
column `c-1` leaves `ext[r] == 0` while `(r,c-1)` is occupied. So the boundary
state carries the previous column's occupancy.

It costs `H+1` bits rather than `2H`. Previous and current column share one
`H`-bit word, because the slot `prev[r]` vacates is exactly the slot `cur[r]`
wants; only `prev[r-1]` needs saving, in a single carry bit reset at each column
start. The standard instance packs into 49 bits.

Every 8-adjacent pair has one member decided strictly before the other, so
checking a cell against its decided neighbours as it is placed catches every
touching pair, and ship halos need no representation at all. The transitions
differ only in which neighbour belongs to the same ship:

```text
horizontal continuation   check (r-1,c-1), (r+1,c-1), (r-1,c)   [(r,c-1) is ours]
vertical continuation     check (r-1,c-1), (r,c-1), (r+1,c-1)   [(r-1,c) is ours]
either start              check all four
empty                     check nothing
```

| quantity | ships may touch | ships may not touch |
| --- | --- | --- |
| configurations | 15,046,987,768 | 1,925,751,392 |
| entropy | 33.8088 bits | 30.8428 bits |
| lattice edges | 28,743,172 | 18,322,562 |
| peak states | 376,735 | 342,892 |
| wall time | 7.78 s | 2.03 s |

Forbidding contact removes 87.20% of the space and 36.3% of the lattice. The
boundary state gained `H+1` bits and the sweep still got faster, because the
adjacency rule kills more profiles than the extra bits create.

The twelve-case small-board ladder agrees across four implementations sharing no
code: this sweep, its brute-force oracle, an independently written row-major
transfer matrix that decides a whole row at a time, and that author's own
enumerator. The 10x10 constant itself is past enumeration and rests on the two
DPs, which were written from the rules alone and agree exactly.
`tests/test_notouch` also checks 240 random constraint patterns against
enumeration, so the constrained counts are covered and not only the prior.

## Soft evidence and a non-uniform prior

Weighting the sweep turns the count into a partition function. Two mechanisms
compose multiplicatively and are independent of each other:

- **per placement**, applied when a ship starts. An opponent model lives here: a
  log-linear prior over placements is just a set of these weights.
- **per cell**, applied to every cell by whether it ends up occupied or empty. An
  observation channel lives here, so a noisy answer contributes its likelihood
  under each hypothesis and one sweep returns the exact normaliser.

Integer exactness is gone, so the path carries its own validation regime.

### Three bridges

Setting every weight to 1 must return the count bit for bit, and does:
15,046,987,768 exactly, in 4.0 s, with no rescaling. The log-linear prior at
`theta = 0` gives the same number, and a sharper check besides: its marginals are
0.0800 at the corner, 0.2136 at the centre and 0.1667 at the edge midpoint, which
is the exact prior table the *integer* path derives. Two routes, same numbers.

The third bridge is at the other end. At `eps = 0.5` every weight is exactly 0.5,
so every board has likelihood `2^-t` whatever it looks like and the evidence must
be `|Omega| * 2^-t`:

```text
   eps   shots   log evidence   vs prior
  0.02      20      12.050395    -16.42b
  0.05      20       9.901802    -19.52b
  0.10      20       9.899525    -19.53b
  0.20      20      10.302840    -18.94b
  0.35      20      10.800053    -18.23b
  0.50      20       9.571500    -20.00b     <- must be exactly -20
  0.50      40      -4.291444    -40.00b     <- must be exactly -40
```

It reads exactly -20.00 and -40.00, which prices the rest of the column at full
10x10 scale without reference to any small board.

Bits against the prior is `log2` of the average likelihood over all
15,046,987,768 boards. It falls with the shot count. In `eps` it is not monotone:
it bottoms out near 0.1 and climbs back, because a channel that noisy stops
punishing disagreement.

### What noise does to the posterior

Thirty shots at a fixed hidden board, then the exact marginals. At `eps = 0.05`
the fleet is legible; the 2-ship at the bottom left is invisible only because no
shot landed near it.

```text
         0      1      2      3      4      5      6      7      8      9
 r0   0.379  0.889  0.866  0.904  0.392  0.018  0.041  0.055  0.045  0.332
 r5   0.251  0.568  0.937  0.942  0.573  0.261  0.024  0.027  0.028  0.278
```

At `eps = 0.20` it does something more interesting than blur. Column 6 reads
0.769, 0.795, 0.733 at rows 4 to 6, while the ship actually sits at rows 6 to 8.
The posterior has not lost the ship, it has moved it, which is what a coherent
model does with corrupted evidence rather than simply widening.

Both heatmaps sum to exactly 17.000000, which is the same invariant the integer
path satisfies and a real check on the weighted marginals. 13 of the top 17 cells
are ships at `eps = 0.05`, and 11 at `eps = 0.20`. Full output in
[docs/WEIGHTED_MARGINALS.txt](docs/WEIGHTED_MARGINALS.txt).

### An opponent who hugs the edge

```text
     edge   vertical          log Z     corner     centre     m(0,4)     m(4,0)
      0.0        0.0      23.434444     0.0800     0.2136     0.1667     0.1667
      1.0        0.0      22.136198     0.0968     0.1531     0.1998     0.1998
      2.0        0.0      21.092172     0.1126     0.1075     0.2301     0.2301
      0.0        1.0      26.599001     0.0780     0.2175     0.1232     0.2021
      2.0        1.0      24.229125     0.1111     0.1080     0.1789     0.2747
```

By `edge = 2` the ordering has inverted: the corner reads 0.1126 against the
centre's 0.1075, where the uniform prior had the centre ahead 2.67 to 1.

The last two columns are a symmetry test rather than a pair of numbers. Cells
(0,4) and (4,0) are reflections of one another, so they must agree whenever the
prior treats the orientations alike, and must part once it does not. They do
both, to every digit printed.

### Numerics

Weighted results agree with an independently written enumerator to within a few
ULP across six cases covering each mechanism alone and both together; the
tolerance is 1e-12 relative, and only the unit-weight cases are bit-exact.

Three numerical audits were run against the first version and all three found
real defects, since fixed. Rescaling divides by a power of two, so it edits
exponents and leaves mantissas untouched and costs no precision. The result is
rebuilt with `ldexp` rather than by multiplying by `exp(logScale)`, which
overflowed once the scale passed 709 even where the product was representable.
And the exactness argument was wrong as first written: intermediate layers are
**not** bounded by the final count, since a layer counts partial placements and a
hard-constrained board can answer in the hundreds of thousands while its layers
still carry billions. The honest bound is the number of partial placements, about
8.0e10 against 2^53, and it is instance-dependent, so `weightedCount` reports the
measured `maxLayerSum` and the tests assert on that rather than on the argument.
The standard instance measures 1.583e10, or 2^33.88.

## The rung that was not one

The bound ladder stops at water filling, 24.088. The plan carried a further rung,
a max-coverage relaxation estimated near 35, which would have closed most of the
remaining interval. It is withdrawn, and `tools/maxcover` is the reason.

Water filling bounds the boards a searcher can have finished by time `t`:

```text
#finished(t)  <=  K * C(t, 17)
```

with `K = 28,560` the feasible hit-transcripts. The argument is an injection.
Under a deterministic policy a finished board is determined by its transcript, and
a transcript is a hit-transcript (`K` ways) interleaved with misses (`C(t,17)`
ways). The proposed improvement replaced the second factor with

```text
maxcov(t) = max over |S| = t of |{ B in Omega : B subset of S }|
```

on the grounds that almost no 17-subset of the shot cells is a legal fleet.

### It fails twice

**It is not even smaller.** `C(t,17)` counts cell subsets; `maxcov` counts
configurations, and several ship decompositions can occupy one cell set. Seventeen
cells shaped as a full row of ten plus seven of the next hold the fleet **20**
different ways against `C(17,17) = 1`. The two cross over near `t = 22`, and only
above that does `C(t,17)` run away.

**It is not substitutable at any `t`.** The factors count different objects.
`K * C(t,17)` works because a finished board is determined by its transcript.
Recovering a bound from `maxcov` instead needs the number of distinct shot-sets a
policy can reach by time `t`, which is the number of length-`t` transcripts and
vastly exceeds `K`.

### Measured, not just argued

On every instance where the optimum is computable, the sketched rung exceeds it:

```text
instance      boards     K  E4 water  adaptive  non-adapt     maxcov   K*maxcov
4x3 {2}           17     1    4.9412    5.1176     7.4706     7.4706     7.4706
4x4 {2}           24     1    5.6667    6.0833     9.5833     9.5833     9.5833
4x4 {3}           16     1    5.0625    5.6250    10.8750    10.7500    10.7500
5x4 {3}           22     1    5.4091    6.2273    13.0455    12.8636    12.8636
4x4 {2,2}        224     2    7.8750    8.6696    12.4866    12.4777     9.9732
4x4 {3,2}        264     5    7.4697    8.7538    13.1098    13.1023     8.7841
```

E4 sits at or below the adaptive optimum on every row, as a bound must. The last
column clears it on every row, so it bounds nothing.

What `maxcov` does obey is the **non-adaptive** optimum, on every row, exactly on
the two single-ship cases and within 0.07% elsewhere. That is the correct reading:
one shot-set of size `t` is precisely the non-adaptive assumption, and an adaptive
searcher has a tree of them. The rung was measuring the wrong problem, and it
overshoots by the adaptivity gap, which `tools/m9` measures at up to 2.09x.

`tools/maxcover selftest` pins this as a negative regression, so the rung cannot
be reintroduced quietly.

### What survives

Two things. First, a measurement of where E4's slack lives: at `t = 90` the
`C(t,17)` factor sits about 1.3e8 above an achievable `c(S)`. That gap is real but
unreachable, since closing it needs a bound on `maxcov` from above and even a
perfect one bounds the non-adaptive problem.

Second, since `maxcov` turned out to be a non-adaptive quantity, the non-adaptive
optimum is worth a number here:

```text
order                        E[T]
row-major                 88.7342
column-major              88.7342
alternating rows          90.2512
diagonal lattice          95.7404
```

Row-major and column-major agree to every digit, which they must: the board is
square and transposing is a bijection on configurations. Compactness is what
matters, and the spread-out diagonal lattice is worst, because a compact prefix is
what contains whole fleets.

So the non-adaptive optimum is at most 88.7342, against a density policy that
measures 44.369. Both are achievable numbers rather than optima, so the pair does
not bound the adaptivity gap from below, but it does show the gap is not small.

## Known limitations

- The `Sampler` holds backward counts for every layer, about 397 MB on the
  standard instance. Batch generation, walking many ranks through one replayed
  column at a time, would remove that; it is not needed until board banks get
  large.
- The no-touching sweep packs its state into one uint64, so it stops at about
  13 rows. `noTouchSupports()` reports whether an instance fits.
- Weighted marginals cost one sweep per cell, about 280 s on the standard
  instance, because there is no weighted forward-backward. The unweighted path
  gets all 100 in two passes and the same trick applies, since the empty
  transition stays state-preserving once it carries a weight.
- The belief MDP caps near 300 configurations, which is what stops the adaptive
  column of the adaptivity table well before the subset lattice runs out.

## Layout

```text
include/mayflower/   constants.hpp (single source of truth), board128, instance,
                     observations, profile_dp
src/core/            the DP, marginals, sampler
tools/               omega0, marginals, sample
tests/oracle/        independent brute-force enumerator and ordered simulator
python/              order-aware reference model
docs/                correctness hazards, complexity notes, M9 results
```

## Licence

[Apache-2.0.](LICENSE.md)
