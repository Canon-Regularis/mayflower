# The optimisation ladder

Four rungs, every one bit-identical, and the measurement protocol that decides
whether a speedup is real.

Part of [Mayflower](../README.md).

V0 is the original DP, frozen as the reference. V1 packs the state into one uint64
and tags slot liveness with an epoch, so one 64-bit compare settles both liveness
and key equality and clearing a layer is an increment, then stages successors so
their probes prefetch and overlap. V2 replaces each cell with a radix-partitioned
scatter into 64 buckets and a per-bucket merge sized to fit L2. V3 spreads that
merge over a persistent thread pool.

A table-size sweep puts the floor at about 76 ns/edge at 16 MB, rising at 8 MB
from probe chains at load factor 0.72 and at 64 MB from address translation. The
DP is memory-latency bound, so the lever is memory-level parallelism and a faster
hash is beside the point. V1 and V2 attack that bottleneck from opposite sides and
land in the same place.

```text
V0  baseline map, 12-byte key struct           1.00x
V1  packed key, epoch tagging, prefetch        1.67x
V2  radix-partitioned scatter and merge        1.69x     (1.01x against V1)
V3  merge across 8 threads                     2.66x     (turns over at 10)
noise floor, A/A control                       1.02x
```

Every rung is bit-identical to V0 across 980 checks covering the small-board
ladder, single-cell fleets, the pinned order-dependence cases, 180 fuzzed ordered
histories and thread counts of 1, 2, 4 and 7. The decomposition guarantees it: counts are integers,
integer addition is associative, and the buckets partition the destination keys so
no two merges touch one counter.

**Measurement protocol.** A run pins to one logical processor of a stated class,
warms up, interleaves the rungs ABBA, and runs an A/A control that measures the
noise floor by comparing a rung against itself. The headline is the ratio of
minima: the computation is deterministic, so every deviation above the fastest run
is interference. A speedup smaller than the measured noise floor is refused rather
than reported. Absolute throughput on this machine is not trustworthy, since the
same workload has been observed between 2.45 s and 204 s and pinning did not
remove the spread; ratios between rungs reproduce, absolute ns/edge figures do
not.
