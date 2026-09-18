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

Every interval `stats.py` produces is calibrated by `stats_test.py`, which simulates
from a known ground truth and counting how often the interval covers it. Coverage of a binomial
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
at 0.807 over 2,000 replicates and 0.825 over the 400 the quick run uses.

## A degenerate pair, and what the artefacts still hold

`density(b=50)` and `density(b=200)` saturate to the same rule: over the whole
20,000-board pool they pick the same cell on every board. Every paired
difference between them is therefore exactly zero, and so is the spread of those
differences, so the paired interval is 0 divided by 0.

`tools/selfplay` used to print that as `[+0.000, +0.000]`, a 95% confidence
interval of zero width. That is not a narrow interval, it is an undefined one,
and printing it as a number claims the difference is known exactly rather than
not estimable. It now prints `identical on all 20000`, and `tools/run_headline`
records `"ci": null` with an `identical` count rather than a pair of zeros.

`experiments/headline_train.json` has been regenerated and carries the new form.
`experiments/headline_test.json` has not, and still holds the zero-width
interval. That is deliberate. Regenerating it means reading TEST, which needs an
unseal recorded in `experiments/audit.log`, and the seal exists so that reading
TEST is a decision someone takes rather than a side effect of tidying a field.
The TEST measurements themselves did not change: the means, the standard
deviations and every other interval are what the current tool produces, and the
`paired` block is read by nothing downstream, so no published number rests on
it. The next legitimate TEST read will carry the corrected form.
