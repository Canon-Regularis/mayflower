# Lower bounds on expected shots

What the optimum is bounded below by, how each rung is established, and the one
rung that was withdrawn.

Part of [Mayflower](../README.md).

```text
E1  coverage        17.0000 shots   all 17 ship cells must be shot
E2  entropy         13.0790 shots   33.8088 bits over an outcome alphabet of 6
E4  water-filling   24.0876 shots   by transcript counting
```

E2 falls below E1, so the entropy bound is dominated and counting ship cells is
the stronger constraint.

Water filling is the strongest rung established here. Against a deterministic
policy the map from configuration to transcript is injective, since the
transcript replays the policy and so reveals which cells were shot and which were
hits. A game ending on shot `t` has 17 hits with the last at `t`, so its
transcript is fixed by choosing the positions of the other 16 among the first
`t-1` and by one of `K` announcement strings:

```text
P(T <= t) <= K * C(t,17) / N        E[T] >= sum_t max(0, 1 - K*C(t,17)/N)

N = |Omega_0| = 15,046,987,768        K = 28,560 announcement strings
```

`K` is computed by determinising the automaton over per-ship hit counts,
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

Unresolved interval `[24.088, 44.369]`, water filling to the best measured
policy, a gap of 20.3 shots. Water filling closes
25.9% of the distance from the coverage bound to the best measured policy.
