# Where the hardness is

Counting fleet configurations is easy here and hard one step away. This records
which step. Sevenster, Crombez and the min-sum set cover line were checked
against the sources; the rest are standard results quoted with their venues.

## 1. What the sweep costs

The DP scans cells column-major carrying `(ext[0..H-1], vrem, fleet_usage)`. For
a fixed height and fixed fleet the state count is a constant and the sweep is
linear in the width. The state is exponential in the height, so on square boards
growing in both directions the cost is `exp(O(sqrt(n)))`. That is the pathwidth
of the grid showing up: the sweep is dynamic programming over a path
decomposition, and the grid's pathwidth is `min(W,H)`.

A state bound for the standard instance. At most five ships exist, so at most
five rows carry a non-zero horizontal residual:

```text
sum_{k=0}^{5} C(10,k) 4^k  =  320,249      profiles
    x 5   vertical residual
    x 24  fleet usage for {5,4,3,3,2}
    =     38,429,880
```

Measured peak is 376,735, a factor of 102 below the bound, because most profiles
are unreachable with the fleet still to place.

## 2. The classic instance is not the hard case

With the fleet fixed at five ships, the configuration count is bounded by the
product of the per-length placement counts, so enumeration is polynomial:

```text
120 x 140 x 160 x 160 x 180 / 2  =  38,707,200,000 ordered tuples
|Omega_0|                        =  15,046,987,768
```

Enumeration examines 2.57 times the answer. So the DP wins nothing in complexity
class. What it wins is size:

```text
28,743,172 lattice edges  against  15,046,987,768 configurations
```

The lattice is 524 times smaller than the set it counts, and the count is read
off without touching a configuration. That is the whole argument for per-turn
exact inference, and it is a statement about constants.

## 3. Three ways to make it hard

### Grow the fleet

A 2-ship occupies two adjacent cells, so a fleet of `m` 2-ships is a matching of
size `m` in the grid graph, and summing over `m` with a fugacity is the
monomer-dimer partition function. Counting matchings in planar graphs is
#P-complete (Jerrum 1987). The fixed fleet is what keeps section 2 polynomial;
letting it grow with the board lands exactly on Jerrum's problem.

### Add row and column sums

Bimaru is the same board with the per-row and per-column occupied counts given.
Sevenster (2004) proves it NP-complete by a parsimonious reduction, which carries
the counting version to #P-complete, and applies Valiant-Vazirani to get
NP-completeness under randomised reductions for the unique-solution promise
variant.

The reduction is for the standard puzzle, where ships may not touch. The rule
that matters for the sweep is the sums, and the sweep's failure is visible
directly. Sweeping column-major, a column sum is checked at the column boundary
and needs only a running counter of `0..H`, multiplying the state by `H+1`. A row
sum accumulates across the whole sweep, so all `H` row counters have to be
carried at once. Measured in [`tools/m9.cpp`](../tools/m9.cpp), section 4.

### Ask for the optimal policy

The belief MDP is a POMDP, and computing an optimal policy for one is
PSPACE-complete (Papadimitriou and Tsitsiklis 1987). The instance here is
degenerate in a useful way: observations are deterministic given the board and
the prior is uniform, so the object is an optimal decision tree over a known
hypothesis set rather than a general POMDP. That is what makes the exact solver
in [`src/search/exact_solver.cpp`](../src/search/exact_solver.cpp) possible at
all, and it still caps out near 300 configurations.

## 4. What is easy next door

Perfect matchings in planar graphs are computable in polynomial time by the FKT
algorithm (Kasteleyn 1961; Temperley and Fisher 1961). So a board tiled
completely by 2-ships is countable in polynomial time, and leaving any cell
empty makes it #P-complete. The gap between those two is one monomer.

## 5. The non-adaptive problem has a name

For a fixed order of the cells, the game ends at the position of the board's last
occupied cell, so the objective is the sum over configurations of the position at
which the configuration is fully covered. In the generalized min-sum set cover
family that is the covering requirement `K(S) = |S|` case, also called min-latency
set cover (Azar, Gamzu and Yin 2009). The familiar `K(S) = 1` version is min-sum
set cover, where greedy is a 4-approximation and `4 - eps` is NP-hard (Feige,
Lovasz and Tetali 2004).

Greedy's factor of 4 does not transfer to `K(S) = |S|`. Constant factors for the
generalized problem do exist and are much larger: 485 (Bansal, Gupta and
Krishnaswamy 2010), improved to 28 (Skutella and Williamson 2011). Section 3 of
`tools/m9` computes the exact optimum by chaining through the subset lattice, so
the greedy order is priced against the truth rather than against any of those,
and it comes within 3.2% on every instance measured.

## 6. Statistical mechanics

Dropping the fleet counter and giving each length a fugacity makes the transfer
matrix column-homogeneous, so `log lambda_max` is a free energy per column. The
system is a hard-rod lattice gas of `k`-mers with `k` in 2..5.

Ghosh and Dhar find an isotropic-nematic transition for `k >= 7`, driven by
density, with a re-entrant isotropic phase near close packing. Both conditions
fail here: the fleet's longest rod is 5, and the occupied fraction is 17/100. The
prediction is no transition, and it is falsifiable by sweeping widths 8 to 14.

The validation runs the other way too. The 2-mer strip entropy extrapolated to
two dimensions lands on 0.6627989727, the monomer-dimer constant of the square
lattice computed rigorously by Friedland and Peled, to within 2.4e-10. Nothing in
the sweep was told that number.

## 7. References

- Jerrum, M. (1987). Two-dimensional monomer-dimer systems are computationally
  intractable. *Journal of Statistical Physics* 48, 121-134.
- Sevenster, M. (2004). Battleships as a decision problem. *ICGA Journal* 27(3).
- Crombez, L., da Fonseca, G. D., Gerard, Y. (2020). Efficient algorithms for
  Battleship. *FUN 2020*, LIPIcs 157. arXiv:2004.07354. One ship of known shape
  placed by translation, minimising worst-case misses before it is located. The
  Battleship complexity is `c(S) <= n-1` for arbitrary shapes, tight for
  parallelogram-free ones, `O(log n)` for HV-convex polyominoes and
  `O(log log n)` for digital convex sets. The 1xL bars used here are HV-convex,
  so the logarithmic bound covers target mode.
- Kasteleyn, P. W. (1961); Temperley, H. N. V. and Fisher, M. E. (1961). Planar
  perfect matchings in polynomial time.
- Papadimitriou, C. H. and Tsitsiklis, J. N. (1987). The complexity of Markov
  decision processes. *Mathematics of Operations Research* 12(3), 441-450.
  POMDPs are PSPACE-complete.
- Feige, U., Lovasz, L., Tetali, P. (2004). Approximating min sum set cover.
  *Algorithmica* 40(4), 219-234.
- Azar, Y., Gamzu, I., Yin, X. (2009). Multiple intents re-ranking. *STOC 2009*.
  Introduces generalized min-sum set cover with covering requirements
  `K(S)` in `1..|S|`; `K(S) = 1` is min-sum set cover and `K(S) = |S|` is
  min-latency set cover, which is the objective in section 5. Their bound is
  `O(log r)` with `r = max |S|`; Bansal, Gupta and Krishnaswamy (2010) give the
  first constant, 485, and Skutella and Williamson (2011) bring it to 28.
- Friedland, S. and Peled, U. N. Monomer-dimer entropy of the square lattice,
  0.6627989727 +/- 1e-10.
- Ghosh, A. and Dhar, D. Isotropic-nematic transition for hard rods on a square
  lattice, present for `k >= 7`.

## 8. What this project adds

Nothing above is new. What is new here is arithmetic on a specific instance:
`|Omega_0| = 15,046,987,768` verified three ways, a lattice 524 times smaller
than the set it counts, exact marginals, a provably uniform sampler, a certified
optimality interval, and the exact non-adaptive and adaptive optima side by side
on instances small enough to solve both.
