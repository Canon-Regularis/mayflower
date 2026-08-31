# Correctness

What is checked against what, and the two hazards that a change to the engine
has to respect.

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

Two things to know before changing the engine:

**Indistinguishable ships need no correction.** The fleet counter records how many
ships of each length have been started, never which, so the DP counts unordered
physical boards. There is no division by `2!`.

**The posterior depends on shot order.** `SUNK(x,L)` means the shot at `x` sank the
ship, so the rest of it was already hit. A predicate requiring only
`cells(ship) subset-of HIT` over-counts, 26 against a true 22 on a reproduced 5x5
case, and two orderings of one shot multiset give 41 and 53. Memo keys must be
order-aware. See [docs/ORDER_DEPENDENCE.md](ORDER_DEPENDENCE.md).
