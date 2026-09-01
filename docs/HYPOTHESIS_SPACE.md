# The hypothesis space

The size of the configuration set, the exact prior it induces, and how both grow
with the board. Every count here is exact; the sampler figures at the end are
measured.

Part of [Mayflower](../README.md).

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

Exact prior occupancy marginals, all 100 cells from one forward and one backward
sweep:

```text
        0      1      2      3      4      5      6      7      8      9
r0  0.0800 0.1149 0.1435 0.1587 0.1667 0.1667 0.1587 0.1435 0.1149 0.0800
r1  0.1149 0.1426 0.1655 0.1777 0.1842 0.1842 0.1777 0.1655 0.1426 0.1149
r2  0.1435 0.1655 0.1841 0.1941 0.1994 0.1994 0.1941 0.1841 0.1655 0.1435
r3  0.1587 0.1777 0.1941 0.2034 0.2084 0.2084 0.2034 0.1941 0.1777 0.1587
r4  0.1667 0.1842 0.1994 0.2084 0.2136 0.2136 0.2084 0.1994 0.1842 0.1667
   (rows 5-9 mirror rows 4-0)
```

Corner 0.0800, centre 0.2136, ratio 2.670, mean exactly 0.17. The 15 dihedral
orbit representatives are exact integers summing with their orbits to
`17 * |Omega_0|`; `tools/marginals` prints them.

Board-size scaling of the same fleet, by the same DP:

| board | 6x6 | 7x7 | 8x8 | 9x9 | 10x10 | 11x11 |
| --- | --- | --- | --- | --- | --- | --- |
| \|Omega\| | 3,343,568 | 62,378,548 | 571,126,760 | 3,394,196,128 | 15,046,987,768 | 54,083,238,912 |

Boards are drawn by unranking, at 19,767 per second after a 20 s build. Over
200,000 draws the largest per-cell deviation from the exact marginal is 0.0022,
which is sampling noise at that size. A biased generator would poison every
statistic drawn from a board pool, so this is a gate rather than a diagnostic.
