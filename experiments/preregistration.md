# Pre-registration

Written before the TEST fold is read. Its purpose is to fix the analysis while
the answer is still unknown, so that a result cannot be selected after the fact.

Anything not written down here is exploratory. Exploratory findings may be
reported, and must be labelled as exploratory.

## The split

Every board id belongs to exactly one fold, decided by hashing the id and
thresholding the result: 60% TRAIN, 20% VAL, 20% TEST. The rule is
[`include/mayflower/folds.hpp`](../include/mayflower/folds.hpp), mirrored in
[`python/stats.py`](../python/stats.py), with both sides asserting the same
pinned vector so they cannot drift.

- **TRAIN** builds and tunes. Unlimited looks.
- **VAL** chooses among finished candidates. Unlimited looks, but no tuning
  against it.
- **TEST** is sealed. Reading it is an event, recorded in
  [`audit.log`](audit.log) before the number may be quoted anywhere.

A board keeps its fold forever, because only its id decides it. Thresholding
rather than a modulus means a later change to the split moves only boards the
moved boundary crosses.

## Headline metrics, fixed in advance

1. **Mean shots to clear**, per policy, on the standard 10x10 `{5,4,3,3,2}`
   instance under the touching ruleset. Primary.
2. **95th percentile shots**, same runs. Secondary, because the tail is what
   loses matches and the mean hides it.
3. **Paired difference** between each policy and the density policy, using
   common random numbers over the same board pool.

Nothing else is a headline. In particular no per-phase, per-region or
per-ship-length breakdown is a headline, and any such number is exploratory.

## Analysis, fixed in advance

- Intervals at 95%, two-sided, alpha = 0.05.
- Means: normal-approximation interval on the paired difference where a pairing
  exists, on the raw mean otherwise.
- Proportions: Wilson score interval. Not the normal approximation, which loses
  four points of coverage at p = 0.01 and is degenerate at k = 0.
- Bootstrap where a statistic has no closed form: percentile, resampling
  **boards**, not moves, because boards are the independent unit.
- Round robin: Holm step-down over the family of pairwise comparisons. No
  independence assumption is available, so no procedure that needs one is used.

The interval code is calibrated rather than trusted: `python/stats.py` simulates
from known ground truth and asserts that a 95% interval covers 95% of the time.
That check is part of the fast test suite.

## Sample sizes, fixed in advance

Derived from the measured spread, sigma = 8.87 shots over 20,000 games, at
alpha = 0.05 and power 0.80.

| effect | paired, rho = 0.923 | paired, rho = 0.00 |
| --- | --- | --- |
| 0.10 shots | 9,510 | 123,506 |
| 0.25 shots | 1,522 | 19,761 |
| 0.50 shots | 381 | 4,941 |
| 1.00 shots | 96 | 1,236 |

The correlation across common random numbers is bimodal here: 0.923 inside the
density family and 0.00 against the stochastic hunt policy, whose variance comes
from its own draws rather than from board difficulty. So the sample size is
derived per comparison from the correlation that comparison actually has, and
never from a single global figure.

At rho = 0 pairing buys nothing and the requirement equals the independent
per-arm figure, so the second column serves as both.

**This supersedes the earlier table.** Its paired figures agree exactly, 9,510 at
0.10 shots. Its independent figures do not: 47,000 was quoted where the formula
and sigma = 8.87 give 123,506. The superseded independent column implies
sigma = 5.47, so the two halves of that table were derived from different
spreads, and the independent half understated the cost by a factor of 2.63.

## Stopping rule

Sample sizes are fixed before the run from the table above. No run is extended
because a result sits near the boundary, and no run is stopped early because it
has already reached significance. Both would invalidate the interval.

## What would falsify the headline claim

The claim is that coverage binds before information: the entropy bound of 13.08
shots is weaker than the coverage bound of 17, so information-greedy play should
underperform hit-greedy play.

It is falsified if the max-information-gain objective matches or beats
max-hit-probability on mean shots, on TEST, outside the interval. That has not
been observed on TRAIN, where information-greedy loses by up to 6.86 shots on
small instances, but TRAIN is not the test.

## Amendments

Any change to this document after the first TEST unseal is an amendment, and is
recorded in `audit.log` with its reason. Amendments do not retroactively cover
numbers already quoted.
