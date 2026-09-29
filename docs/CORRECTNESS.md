# Correctness

What is checked against what, and what a change to the engine has to respect.

Part of [Mayflower](../README.md).

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

## What happens when the code is wrong

Everything above is a test passing. A test that passes over code that cannot
fail it proves nothing, and this repository has found that shape often enough to
stop taking a green suite as an answer. So the complementary question is asked
directly: plant a fault, and see what notices.

`tools/mutants.json` holds eight faults as data. Each carries the file, the
exact edit that plants it, and per ctest label the tests that must fail because
of it. `tools/mutate.py` applies them one at a time: plant, rebuild, run the
label, restore, and compare the verdict against what the file records. The file
is a check rather than a log, so a run that disagrees with it exits non-zero and
says which entry moved. An empty list is a finding and not an omission, and
clearing one to make a run green is the one thing the file asks you not to do.

```sh
python tools/mutate.py --list
python tools/mutate.py            # every fault, every label its entry records
```

Seven of the eight are caught. One is not.

| the fault planted | `-L fast` | `-L pr` |
| --- | --- | --- |
| the per-placement horizontal gate dropped | fuzz, gated, ladder, observations, sampler | |
| the same on the vertical axis | fuzz, gated, ladder, observations, sampler | |
| the TRAIN fold boundary moved | folds | |
| `unrank` accepts a rank one past the end | sampler | |
| information gain scored over a binary channel | nothing | outcomes |
| the star1 floor inflated past the true remainder | nothing | exact |
| the density policy's tie-break reversed | nothing | harness |
| the weighted exactness flag stops watching underflow | nothing | **nothing** |

The last row is the standing finding. `weightedCount` reports `exact` as the
flag a caller reads to decide whether a result is bit-trustworthy, and it is
withheld on rescaling, on underflow, and on a layer sum past 2^53. Remove only
the underflow clause and every test still passes: the weighted cases all sit far
inside the other two limits, so the one that watches for lost configurations has
never been the reason a test failed.

Two rows moved while this was being written, and both are worth recording.
`unrank` accepting an out-of-range rank was caught by nothing in an earlier
campaign and is caught by `sampler` now, because `test_sampler.cpp` gained
`testUnrankRefusesRanksOffTheEnd`, whose own comment records that the guard
existed and nothing had ever reached it. The density policy's tie-break was
caught by nothing in either label: `test_harness.cpp` computed the cell it opens
on and printed it, which is this project's recurring defect shape, and it now
asserts it. That one mattered more than it looks. Every measured density row in
the report is a sum over games whose every tied choice that comparison decides,
so reversing it moves all of them at once while leaving each one individually
plausible.

### Two things about the method, which cost more to learn than the table

**A test can fail under every mutant and witness none of them.**
`tests/test_provenance.py` asserts that no engine source is newer than
`out/figures.json`. Planting a fault edits an engine source, so it fails under
every mutant by construction and for the same reason each time. The first run of
this driver reported all eight faults as caught, including the four nothing else
touched. `tools/mutate.py` excludes it from every verdict and says why.

**A skip is not a witness.** Thirteen tests in the fast label are `SKIPPABLE`
and `out/` is gitignored, so on a fresh clone six of them skip. `ctest` writes
`name (Skipped)` in the same shape as `name (Failed)`, so reading that block
without the status counts a test that did not run as one that caught something,
and every fault would come back caught on any machine that had not first spent
twelve minutes generating the figure data. The driver reads the status, and a
run in which anything skipped cannot establish that a fault survived either: the
test that would have caught it may be one of the ones that did not run. That
verdict is `INCONCLUSIVE` and names them.

Both are the same error in opposite directions, and it is the one this file
exists to guard against: a verdict that does not depend on its subject.

Three things to know before changing the engine:

**Indistinguishable ships need no correction.** The fleet counter records how many
ships of each length have been started, never which, so the DP counts unordered
physical boards. There is no division by `2!`.

**The posterior depends on shot order.** `SUNK(x,L)` means the shot at `x` sank the
ship, so the rest of it was already hit. A predicate requiring only
`cells(ship) subset-of HIT` over-counts, 26 against a true 22 on a reproduced 5x5
case, and two orderings of one shot multiset give 41 and 53. Memo keys must be
order-aware. See [ORDER_DEPENDENCE.md](ORDER_DEPENDENCE.md).

**A length-1 ship starts horizontally only.** Both branches of the cell sweep
would emit the same single cell, so a rung without the `L > 1` guard returns
`2^k` times the truth on a fleet of `k` single cells. Five of the six sweeps
shipped without it: the four C++ rungs other than the no-touching one, which
carried the guard from its first commit, plus the browser engine. The ladder compares the rungs against each other, and every
case in its list had `L >= 2`, so it could not distinguish them; the list now
carries single-cell fleets.
