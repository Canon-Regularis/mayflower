# Experimental discipline

Folds, the seal on the TEST data, calibrated intervals, and how sample sizes are
derived.

Part of [Mayflower](../README.md).

Every board id belongs to exactly one of TRAIN, VAL or TEST, decided by hashing
the id and thresholding 60 / 20 / 20, so a board keeps its fold forever.
Thresholding rather than a modulus is deliberate: moving a boundary later moves
only the boards that boundary crosses, where a modulus would reshuffle the whole
space. The rule lives in `include/mayflower/folds.hpp` and `python/stats.py`, and
both assert the same pinned vector, so drift fails the build on either side.
`tools/selfplay` defaults to TRAIN and refuses TEST.

TEST is sealed. Reading it requires an unseal entry in
[`experiments/audit.log`](../experiments/audit.log) recorded before the number is
read, and `audit.log.head` records how many entries there should be and what the
last hash is, which catches an edit, an interior deletion, a truncated tail, and
an entry commented out so it stays visible while leaving the chain. It is not
tamper-proof: the digest takes only public inputs, so anyone with write access can
recompute the history. What the chain buys is that tampering costs a rewrite of
the log, the head and the version history containing them, rather than an edit to
one line. Git is the anchor.

Every interval `stats.py` produces is calibrated by simulating from a known ground
truth and counting how often the interval covers it. Coverage of a binomial
interval is a finite sum, so it is computed rather than sampled. Wilson lands
closer to 95% than the normal approximation at every `p` at or below 0.10, by a
factor of fifty at `p = 0.01`, and the two coincide at `p = 0.5`.

Sample sizes, both columns paired designs, at alpha 0.05 and power 0.80:

| effect | rho = 0.923 | rho = 0.00 |
| --- | --- | --- |
| 0.10 shots | 9,510 | 123,506 |
| 0.25 shots | 1,522 | 19,761 |
| 0.50 shots | 381 | 4,941 |
| 1.00 shots | 96 | 1,236 |

At rho = 0 pairing buys nothing and the requirement equals the independent
per-arm figure, so the second column serves as both. Because the correlation is
bimodal, the same comparison against two opponents differs by a factor of
thirteen and no single number covers both. The formula is checked by simulating at
exactly the prescribed `n` and confirming the promised 80% power arrives, measured
at 0.807.
