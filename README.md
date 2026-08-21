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

Milestones M0 to M2 are complete, and M3 has its harness, board bank and cheap
policy tier.

| component | |
| --- | --- |
| `src/core/profile_dp.cpp` | The DP, parameterised over board size and fleet |
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
| `src/search/exact_solver.cpp` | Exact optimal play on small instances |
| `tools/optimal` | Optimal play and the measured optimality gap of each objective |
| `src/core/profile_dp_fast.cpp` | Ladder rung V1: packed key, epoch tagging, batched prefetch |
| `src/platform/` | Core-topology detection and thread pinning for benchmarking |
| `bench/dp_bench` | The ladder under the measurement protocol |
| `outcomeDistribution` | Exact one-ply {MISS, HIT, SUNK(L)} split and information gain per cell |
| `include/mayflower/policy.hpp` | Random, parity hunt/target, density (cheap tier), and exact-posterior policies |
| `tests/oracle/` | Independent brute-force enumerator and ordered simulator |
| `python/oracle.py` | Order-aware reference model |

Still to come: the bound ladder, lookahead and the endgame solver, the
optimisation ladder, and the report pipeline. The no-touching ruleset is not
implemented.

## Build

C++20, CMake >= 3.24, Ninja. Developed against MinGW-w64 GCC 13.2 on Windows.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

ctest --test-dir build -L fast     # 3 suites, ~2 s
ctest --test-dir build -L pr       # adds the full 10x10 runs, ~2.5 min

./build/omega0                     # the hypothesis-space size
./build/marginals                  # the exact prior heatmap
./build/sample                     # uniform board generation
python python/oracle.py --order-dependence
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
claimed. The water-filling and max-coverage rungs are not implemented and are
therefore not quoted either.

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

## Known limitations

- The `Sampler` holds backward counts for every layer, about 397 MB on the
  standard instance. Batch generation, walking many ranks through one replayed
  column at a time, would remove that; it is not needed until board banks get
  large.
- The no-touching ruleset is not implemented, so `tools/omega0` reports only the
  touching count.

## Layout

```text
include/mayflower/   constants.hpp (single source of truth), board128, instance,
                     observations, profile_dp
src/core/            the DP, marginals, sampler
tools/               omega0, marginals, sample
tests/oracle/        independent brute-force enumerator and ordered simulator
python/              order-aware reference model
docs/                correctness hazards
```

## Licence

Apache-2.0.
