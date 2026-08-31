# Mayflower

[![CI](https://github.com/Canon-Regularis/mayflower/actions/workflows/ci.yml/badge.svg)](https://github.com/Canon-Regularis/mayflower/actions/workflows/ci.yml)

Exact Bayesian inference and optimisation, with Battleships as the problem
instance.

Mayflower maintains the exact posterior over every fleet configuration consistent
with an observation record. A broken-profile transfer-matrix DP replaces
enumeration of 15,046,987,768 configurations with one sweep over a lattice of
2.87e7 edges, so the posterior after any record is a counting problem rather than
a sampling one. On that posterior it prices shot-selection objectives against
certified lower bounds.

## Results

**The space, and the sweep that avoids it.** A 10x10 board holding `{5,4,3,3,2}`
admits exactly **15,046,987,768** arrangements. The sweep counts them over
**28,743,172** edges, 523 boards accounted for per edge relaxed, and returns all
100 exact cell marginals in one forward and one backward pass. The marginal runs
from 0.0800 at a corner to 0.2136 at the centre and sums over the board to exactly
17.

**Coverage binds, information does not.** Identifying the board costs 33.81 bits.
Each shot answers over an alphabet of six, worth log2(6) = 2.585 bits, so a
44-shot game supplies 114.7 bits against the 33.81 it needs, a surplus of
3.4 to 1. The entropy floor is therefore 13.08 shots, below the trivial coverage
bound of 17 and dominated by it. The rung that binds counts finished games
instead.

| bound | shots | how |
| --- | --- | --- |
| entropy | 13.079 | H0 / log2 6, dominated by coverage |
| coverage | 17 | all 17 ship cells must be shot |
| water filling | 24.088 | transcript counting, the binding rung |
| best measured | 44.369 | density policy, 20,000 seeded boards |

The interval `[24.088, 44.369]` is **20.28 shots** nobody has closed. It holds
both the true optimum and the loss of the best rule against it, and nothing
measured here separates the two. See [docs/BOUNDS.md](docs/BOUNDS.md).

**Playing for information is provably wrong.** On instances small enough to solve
outright, every policy is priced against the true optimum with no sampling error.
Maximising hit probability is exactly optimal on three of six and never more than
0.1553 shots off. Maximising one-step information gain loses up to **6.8636
shots**, more than doubling the optimum on 5x4 {3}. The mechanism is in the
objective: a binary answer yields H(p), which peaks at p = 1/2, so the rule turns
away from a cell exactly as the evidence starts to favour it. See
[docs/OPTIMAL_PLAY.md](docs/OPTIMAL_PLAY.md).

**A policy that reconstructs its own prior.** The density policy hard-codes no
geometry; it counts placements of the remaining fleet covering each cell. Its mean
shot turn against the exact prior marginals gives a rank correlation of
**-0.878**, opening on the centre at mean turn 1.00 and reaching the far corner at
43.39.

## Why it is interesting

Three things make the exact treatment possible.

**The posterior is a partition function.** Conditioning on a record is a filter on
the sweep rather than a resampling, so `|Omega(O)|` and every cell marginal come
out exact for any record at any depth. Ten misses cut the live state set to 12.7%
of the prior and thirty to 0.4%, and the cost falls with them: a counting sweep
never backtracks, so the hardest record is cheaper than the empty one.

**The profile is small because most of it is unreachable.** The boundary state
carries a residual extension per row, a vertical run, and how much of the fleet
has been started. Those fields admit 5^11 x 24 = 1,171,875,000 profiles, and the
sweep never holds more than 376,735 at once, a factor of 3,111, because no legal
partial placement produces the rest.

**Adaptive policies are evaluated rather than estimated.** On small instances the
belief MDP gives the true optimum, so an objective's cost is a difference of
integers over the whole space instead of a difference of sample means. That is
what makes "provably suboptimal" a statement rather than a suspicion: greedy hit
probability spends 2352 shots across 4x4 {3,2} where optimal play spends 2311, a
difference of exactly 41.

## Quick start

From a clean checkout to the report open in a browser. Timings are from the machine 
I developed this project on - your results may differ by a relative margin.

```sh
git clone https://github.com/Canon-Regularis/mayflower && cd mayflower

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   #  11 s
cmake --build build                                       #  51 s
ctest --test-dir build -L fast                            #  33 s, 15 tests

./build/omega0            # 15,046,987,768 and 1,925,751,392, plus the lattice
./build/bounds            # the ladder, and the interval [24.088, 44.369]
./build/optimal           # exact optimal play, and the price of each objective

mkdir -p out
./build/report_data 20000 > out/figures.json              # 12 min
python tools/render_report.py out/figures.json out/report.html
python tools/collect_results.py && python tools/render_results.py
```

Then open `out/report.html`. It is one self-contained file and needs no server.

C++20, CMake >= 3.24, Ninja, Python 3 and Node. Python is standard library only,
so there is nothing to install. Developed against MinGW-w64 GCC 13.2 on Windows;
CI also builds Linux GCC and Clang.

Two tests report `Skipped` until `report_data` has run, because they read the
generated `out/figures.json`. That is expected on a fresh clone. `report_data` is
dominated by fixed exact sweeps rather than by the game count, so a smaller number
buys little: 200 games costs 387 s against 717 s for the 20,000 above.

## The report

`out/report.html` is the deliverable: one file, opening with an engine that hunts a hidden board using a real posterior and
closing with one recorded game replayed frame by frame at the exact marginals.

`out/results.html` is the companion: 96 recorded quantities, 71 exact and 25
measured, each labelled by how firmly it is established and by which tool printed
it.

## Architecture

| | |
| --- | --- |
| `src/core/` | The sweep: profile DP and its rungs, marginals, sampler, no-touching, weighted counting |
| `src/search/` | The belief MDP and its pruning |
| `src/certify/` | Blocking numbers and announcement transcripts |
| `src/lattice/` | The transfer matrix |
| `tools/` | One executable per result, plus the report and results renderers |
| `web/` | The DP in JavaScript, the live widget, the belief scrubber |
| `tests/oracle/` | An independent enumerator sharing nothing with the engine |
| `python/` | Order-aware reference model, bond dimension, the analysis layer |
| `experiments/` | Pre-registration, registry, append-only audit log |

`tests/oracle/` includes nothing from `include/mayflower/`, and `python/` reaches
the renderers only through the figure-data contract.

## Correctness

The DP agrees exactly with brute-force enumeration on nine reduced instances, and
`unrank` is checked exhaustively on four, enumerating the configuration set once
per rank, which proves uniformity outright rather than by a statistical argument.
Three independent implementations of the sweep exist in C++, JavaScript and
Python, and the JavaScript engine is checked against the Python oracle, which
shares code with neither.

Two hazards a change to the engine has to respect.

**Order is part of the record.** `SUNK(x,L)` means the shot at `x` sank the ship,
so the rest of it was already hit. A predicate requiring only
`cells(ship) subset-of HIT` over-counts, 26 against a true 22 on a reproduced 5x5
case, and two orderings of one shot multiset leave 41 and 53 configurations
standing. See [docs/ORDER_DEPENDENCE.md](docs/ORDER_DEPENDENCE.md).

**A length-1 ship has one placement.** Four of the five sweeps emitted it from
both the horizontal and the vertical branch, so a fleet of k single cells came
back 2^k times too large. Both brute-force oracles always carried the guard.

Full detail in [docs/CORRECTNESS.md](docs/CORRECTNESS.md).

## Further reading

| | |
| --- | --- |
| [docs/HYPOTHESIS_SPACE.md](docs/HYPOTHESIS_SPACE.md) | The exact prior marginals, the orbit integers, board-size scaling, and the sampler |
| [docs/BOUNDS.md](docs/BOUNDS.md) | The ladder, water filling, blocking numbers, and the rung that was withdrawn |
| [docs/OPTIMAL_PLAY.md](docs/OPTIMAL_PLAY.md) | The belief MDP, the objective comparison, and measured self-play |
| [docs/WEIGHTED.md](docs/WEIGHTED.md) | Opponent priors, noisy channels, and where the floating point stops being exact |
| [docs/RULESETS.md](docs/RULESETS.md) | The no-touching rule, and the variants that reuse the engine |
| [docs/TRANSFER_MATRIX.md](docs/TRANSFER_MATRIX.md) | The hard-rod lattice gas, and how close the boundary state is to minimal |
| [docs/BENCHMARKS.md](docs/BENCHMARKS.md) | The optimisation ladder and its measurement protocol |
| [docs/EXPERIMENTS.md](docs/EXPERIMENTS.md) | Folds, the seal, calibrated intervals, sample sizes |
| [docs/CORRECTNESS.md](docs/CORRECTNESS.md) | What is checked against what |
| [docs/COMPLEXITY.md](docs/COMPLEXITY.md) | Which step makes this hard, with the references checked |
| [docs/CI.md](docs/CI.md) | What runs on a push, on a pull request, and nightly |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | What does not work, or does not scale |

Captured tool output sits beside them: [M9_RESULTS.txt](docs/M9_RESULTS.txt),
[MAXCOVER.txt](docs/MAXCOVER.txt), [OPPONENT.txt](docs/OPPONENT.txt),
[WEIGHTED_MARGINALS.txt](docs/WEIGHTED_MARGINALS.txt).

## Licence

[Apache-2.0.](LICENSE)
