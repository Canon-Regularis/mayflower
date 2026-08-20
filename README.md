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

Milestones M0 to M2 are complete.

| component | |
| --- | --- |
| `src/core/profile_dp.cpp` | The DP, parameterised over board size and fleet |
| `include/mayflower/observations.hpp` | Ordered observation record and the placement predicate it induces |
| `occupancyMap` | Every cell marginal from one forward and one backward sweep |
| `Sampler` | Exact uniform sampling by unranking; the board generator |
| `tools/omega0` | Reproduces \|Omega_0\| and the lattice statistics |
| `tools/marginals` | The exact prior heatmap and the 15 D4 orbit integers |
| `tools/sample` | Uniform board generation, checked against the exact marginals |
| `tests/oracle/` | Independent brute-force enumerator and ordered simulator |
| `python/oracle.py` | Order-aware reference model |

Still to come: the policy layer, the bound ladder, lookahead and the endgame
solver, the optimisation ladder, and the report pipeline. The no-touching
ruleset is not implemented.

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
