# The transfer matrix, and the bond dimension

Battleships as the fixed-fleet corner of a hard-rod lattice gas, and how close
the boundary state is to minimal.

Part of [Mayflower](../README.md).

## The transfer matrix

Drop the fleet counter, give each rod a fugacity, and the column operator becomes
the same at every column. Battleships is the fixed-fleet corner of a hard-rod
lattice gas. `lambda_max` comes from power iteration where applying the operator is
one column sweep of the same DP, so no matrix is ever formed; the subdominant
eigenvalue falls out of the convergence rate at a lag of two, because for these
strips it is negative and the correction alternates sign.

Three checks the code was not given:

```text
1-row dimer strip counts Fibonacci      1.618033988750  against the golden ratio
eigenvalue against a finite patch       3.7545140595    against Z(49)/Z(48)
entropy per site, extrapolated to 2D    0.6627990       against 0.6627989727
```

The last is the monomer-dimer entropy of the square lattice, reached from strip
widths 2 to 12 with no input beyond the sweep. The Battleship instance sits at
0.2343 nats per site, well below the free gas, because fixing the rod count turns
a thermodynamic problem into a counting one.

## Bond dimension

The sweep is a matrix-product contraction, so the number of distinct boundary
states is its bond dimension. Cutting between two columns and building the
compatibility matrix `M[l][r]` gives the Schmidt rank, a floor for any linear
representation; the count of distinct rows of `M`, which is the Myhill-Nerode
count and the true floor for a state-based sweep; and what the engine carries.

The engine's state runs 1.86 to 2.80 times larger than the Nerode minimum across
every cut and instance tested, with no sign of the ratio growing. The
representation is sufficient and not minimal, and there is roughly a factor of two
of algorithmic headroom that no amount of cache tuning would reach. What stops the
engine claiming the minimum is that its state is a sufficient statistic computable
from the cells already swept, while a Nerode class is defined by the completions
that follow. Making the second computable locally is open.
