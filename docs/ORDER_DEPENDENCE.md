# The posterior depends on shot order

Read this before touching anything that consumes an observation record. Getting
it wrong does not crash; it inflates hypothesis counts, which propagates into
every probability, every information-gain score and every reported number.

## The rule

`SUNK(x, L)` means the shot at cell `x` sank the ship, so every other cell of
that ship had already been shot strictly before `x`.

The predicate

```text
cells(ship) subset-of HIT   and   length(ship) == L        // WRONG
```

is sound but incomplete. It admits configurations that could not have produced
the announcement, so it over-counts.

## Reproduced counterexamples

Both from `python/oracle.py --order-dependence` on 5x5 `{3,2,2}` (12,798 boards),
by literal enumeration.

### 1. Two orderings of the same shots give different posteriors

```text
order A  (1,1):HIT (2,1):HIT (3,3):MISS (1,2):HIT (2,0):SUNK2 (0,0):MISS (4,1):MISS
         |Omega| = 41

order B  (1,2):HIT (1,1):HIT (4,1):MISS (3,3):MISS (2,0):HIT (0,0):MISS (2,1):SUNK2
         |Omega| = 53
```

Identical shot multiset, identical outcome multiset, different posterior.

### 2. The order-free predicate over-counts

```text
history  (4,1):HIT (3,4):MISS (3,1):SUNK2 (4,3):MISS (2,1):HIT
         (4,4):MISS (4,2):MISS (0,0):MISS (1,3):HIT

ordered     |Omega| = 22
order-free  |Omega| = 26      (over by 4, 18%)
```

## Consequences

1. **Memo and transposition keys must be order-aware.** The key is
   `(miss bitmask, ordered sequence of HIT/SUNK events)`. An order-free
   `{misses, hits, sunk[]}` triple gives wrong answers.

2. **The available invariance is weaker than set-invariance.** The posterior is
   invariant under permutations that preserve the relative order of all HIT and
   SUNK shots; misses may move anywhere. That is the strongest canonicalisation
   available.

3. **A sunk ship's cells cannot simply be pinned and forgotten.** Which cells
   belonged to the sunk ship can itself be ambiguous, and order constrains the
   ambiguity.

4. **There is a mandatory negative regression test.** `tests/` must assert that
   the order-free predicate disagrees with the ordered one on the cases above.
   Without it, a later change that canonicalises to a set passes everything else
   in the suite.

## Current status

`include/mayflower/profile_dp.hpp` exposes per-cell constraints only (`Free`,
`MustBeEmpty`, `MustBeOccupied`), so no caller can obtain wrong sunk semantics
today. `python/oracle.py` implements the correct ordered model and is the
reference the C++ sunk predicate must match when it lands in M1.
