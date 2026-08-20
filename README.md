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

Milestones M0 and M1 (foundations, exact counting core) are landing.

| | |
| --- | --- |
| `tools/omega0` | Reproduces \|Omega_0\| = 15,046,987,768 and the lattice statistics |
| `src/core/profile_dp.cpp` | The DP, parameterised over board size and fleet |
| `tests/oracle/` | Independent brute-force enumerator sharing no code with the engine |
| `python/oracle.py` | Order-aware observation-model reference |

Still to come: forward-backward marginals, the unranking sampler, the policy
layer, the bound ladder, the report pipeline.

## Build

C++20, CMake >= 3.24, Ninja. Developed against MinGW-w64 GCC 13.2 on Windows.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/test_counting          # DP vs brute force, ~0.1 s
./build/omega0                 # ~9 s
python python/oracle.py --order-dependence

ctest --test-dir build -L fast
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

Board-size scaling of the same fleet, all by the same DP:

| board | \|Omega\| |
| --- | --- |
| 6x6 | 3,343,568 |
| 7x7 | 62,378,548 |
| 8x8 | 571,126,760 |
| 9x9 | 3,394,196,128 |
| 10x10 | 15,046,987,768 |
| 11x11 | 54,083,238,912 |

The DP agrees exactly with brute-force enumeration on nine reduced instances,
including repeated ship lengths and non-square boards:

```text
4x4 {3,2}       264        6x6 {3,3,2}      40,324      6x6 {4,3,3,2}  633,432
5x5 {3,2,2}  12,798        6x6 {4,3,2}      53,624      4x6 {3,2}          840
5x5 {4,3,2}   9,024        5x5 {3,3,2,2}    80,688      7x5 {4,3,2}     46,226
```

The invariant `sum over cells of P(cell occupied) = 17` holds in exact integer
arithmetic.

## Two things to know before contributing

**Indistinguishable ships need no correction.** The fleet counter records how
many ships of each length have been started, never which, so the DP counts
unordered physical boards. There is no division by `2!`.

**The posterior depends on shot order.** `SUNK(x,L)` means the shot at `x` sank
the ship, so the rest of it was already hit. A predicate requiring only
`cells(ship) subset-of HIT` over-counts: 26 against a true 22 on a reproduced 5x5
case. Memo keys must be order-aware. See
[docs/ORDER_DEPENDENCE.md](docs/ORDER_DEPENDENCE.md).

## Layout

```text
include/mayflower/   constants.hpp (single source of truth), board128, instance, profile_dp
src/core/            the DP
tools/               omega0
tests/oracle/        independent brute-force enumerator
python/              order-aware reference model
docs/                correctness hazards
```

## Licence

Apache-2.0.
