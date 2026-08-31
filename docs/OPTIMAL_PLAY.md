# Exact optimal play, and what each objective costs

The belief MDP on instances small enough to solve outright, the three shot-
selection objectives priced against it, and measured self-play at full scale.

Part of [Mayflower](../README.md).

## Optimal play

Where the configuration space is enumerable, the optimum and each policy's
expectation are both exact, so the optimality gap is measured rather than
estimated.

```text
instance       cfgs   optimal   density     gap    parity     gap
3x3 {2}          12    4.5000    4.5000   0.000    5.0000   0.500
4x3 {2}          17    5.1176    5.1176   0.000    5.6471   0.529
4x4 {2}          24    6.0833    6.0833   0.000    6.6250   0.542
4x4 {3}          16    5.6250    5.7500   0.125    6.7500   1.125
5x4 {3}          22    6.2273    6.2273   0.000    8.1364   1.909
4x4 {2,2}       224    8.6696    8.7232   0.054    9.4196   0.750
4x4 {3,2}       264    8.7538    8.8902   0.136    9.7955   1.042
```

The density heuristic is exactly optimal on four of seven instances and within
0.14 shots on the rest. The optimal first shot is an off-corner cell in every
case.

The same machinery prices the three shot-selection objectives. Totals are integers
over the enumerated space, so these gaps carry no sampling error.

```text
instance      cfgs    optimal   max-P(hit)     gap     max-info      gap
4x4 {3,2}      264   8.753788     8.909091  0.1553    12.662879   3.9091
4x4 {2,2}      224   8.669643     8.741071  0.0714    11.401786   2.7321
4x4 {3}         16   5.625000     5.750000  0.1250    10.500000   4.8750
4x3 {2}         17   5.117647     5.117647  0.0000     6.588235   1.4706
3x3 {2}         12   4.500000     4.500000  0.0000     5.500000   1.0000
5x4 {3}         22   6.227273     6.227273  0.0000    13.090909   6.8636
```

Maximising hit probability is exactly optimal on three of six and never worse
than 0.16 shots, so it is a strong heuristic and provably not the optimum.
Maximising one-step information gain is worse by whole shots: on 5x4 {3} it takes
13.09 against an optimum of 6.23.

The belief MDP's memo key is a shot mask plus the surviving support, which is
already the sufficient statistic, so no order-aware transposition table is needed.
Pruning is star1's chance-node bound with move ordering by descending hit
probability, worth a factor of 158 on the fleet instances.

## Self-play

One seeded pool of uniform boards, 20,000 games, every policy on the same boards.

```text
policy                   mean      sd     95% CI on mean  median    p95   best  worst
random                 95.354   4.800  [ 95.288,  95.421]      97    100     57    100
parity-hunt-target     51.535   8.710  [ 51.414,  51.656]      53     64     21     71
density                44.369   8.868  [ 44.246,  44.491]      44     61     20     85
```

The random shooter is the harness self-test: shooting uniformly, the game ends on
the last of the 17 ship cells, so `E[T] = k(N+1)/(k+1) = 95.3889`. Measured
95.3544, inside the interval.

Correlation across the shared pool is bimodal, so sample sizes have to be derived
per comparison. Within the density family it is 0.923, worth 12.9 times fewer
games for the same precision. Against the stochastic hunt policy it is zero,
because that policy's variance comes from its own draws rather than from board
difficulty, so pairing buys nothing there.
