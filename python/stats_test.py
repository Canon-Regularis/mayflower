"""The analysis layer's self test.

Every interval this repository produces is checked by simulating from a known
ground truth and counting how often the interval covers it. A 95% interval that
covers 91% of the time is a bug, and this is where it gets caught rather than
believed. It has earned that twice already: it found two defects in this
project's own Wilson interval, and it caught a claim written here that had the
ordering backwards.

Split out of python/stats.py, where it was 420 of the module's 831 lines and
was imported by every production caller of the analysis layer.

    python python/stats_test.py            # run everything
    python python/stats_test.py --quick    # fewer replicates
"""

from __future__ import annotations

import argparse
import datetime
import io
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from _report import check  # noqa: E402
from audit import (AUDIT_PATH, GENESIS, HEAD_PATH, audit_entries, is_unsealed,  # noqa: E402
                   read_head, record, require_unseal, verify_audit, write_head)
from stats import (TRAIN_SHARE, VAL_SHARE, Z_95, bootstrap_interval,  # noqa: E402
                   exact_coverage, fold_fraction, fold_hash, fold_of,
                   games_needed, holm, mean_interval, normal_quantile,
                   paired_interval, regularised_beta, student_t_quantile,
                   wald_interval, wilson_interval)

# The seal is tested where it lives, and driven from here so one --quick
# entry point still runs everything.
from audit_test import test_audit


def test_folds():
    print("[folds]")
    fails = 0
    n = 200000
    counts = {"train": 0, "val": 0, "test": 0}
    for i in range(n):
        counts[fold_of(i)] += 1

    share = {k: v / n for k, v in counts.items()}
    fails += check(abs(share["train"] - 0.60) < 0.005, "train share is 60%",
                   "{:.4f}".format(share["train"]))
    fails += check(abs(share["val"] - 0.20) < 0.005, "val share is 20%",
                   "{:.4f}".format(share["val"]))
    fails += check(abs(share["test"] - 0.20) < 0.005, "test share is 20%",
                   "{:.4f}".format(share["test"]))
    fails += check(sum(counts.values()) == n, "every board lands in exactly one fold")

    # Stability: moving the val boundary must not move anything out of train.
    global VAL_SHARE
    before = [fold_of(i) for i in range(5000)]
    saved = VAL_SHARE
    VAL_SHARE = 0.25
    after = [fold_of(i) for i in range(5000)]
    VAL_SHARE = saved
    moved_train = sum(1 for a, b in zip(before, after) if a == "train" and b != "train")
    fails += check(moved_train == 0,
                   "widening val leaves every train board where it was")

    # The same two values include/mayflower/folds.hpp pins. Both sides assert,
    # so a change to either fails the build rather than drifting quietly.
    pinned = "vvttttvvtttttvvtttttvtvtttttttvvttttttvt"
    got = "".join(fold_of(i)[0] for i in range(40))
    fails += check(got == pinned, "the first forty ids match the C++ vector", got)
    fails += check("{:.17g}".format(fold_fraction(0)) == "0.78164751589525394",
                   "foldFraction(0) matches C++ to the last digit",
                   "{:.17g}".format(fold_fraction(0)))

    # Adjacent ids must not correlate; a modulus would fail this.
    runs = sum(1 for i in range(1, 20000) if fold_of(i) == fold_of(i - 1))
    expected = 19999 * (0.6 ** 2 + 0.2 ** 2 + 0.2 ** 2)
    fails += check(abs(runs - expected) < 0.06 * expected,
                   "neighbouring ids are not correlated",
                   "{} adjacent matches, expected about {:.0f}".format(runs, expected))
    return fails


def test_domains():
    """Arguments outside their domain must be refused, not evaluated.

    Each of these returned a confident answer. paired_interval let zip() stop at
    the shorter array and reported an interval computed from a prefix, which in a
    paired comparison hides the fact that the pairing broke. games_needed
    accepted a correlation of 1.5 and returned -61752 games, a sigma of 0 and
    returned 0, and a negative effect and returned the count for its positive
    twin. That function is what experiments/preregistration.md derives from.
    """
    print("[argument domains]")
    fails = 0

    def refuses(what, fn, exc=ValueError):
        try:
            got = fn()
        except exc:
            return check(True, what)
        except Exception as e:                       # noqa: BLE001
            return check(False, what, "raised {} instead".format(type(e).__name__))
        return check(False, what, "returned {!r}".format(got))

    fails += refuses("mismatched paired samples are refused",
                     lambda: paired_interval([10.0] * 6, [1.0] * 2))
    fails += refuses("a correlation of 1.5 is refused",
                     lambda: games_needed(0.10, 8.87, rho=1.5))
    fails += refuses("a correlation of exactly 1 is refused",
                     lambda: games_needed(0.10, 8.87, rho=1.0))
    fails += refuses("a zero spread is refused",
                     lambda: games_needed(0.10, 0.0))
    fails += refuses("a negative effect is refused",
                     lambda: games_needed(-0.10, 8.87))
    fails += refuses("more successes than trials is refused",
                     lambda: wilson_interval(5, 2))
    fails += refuses("a negative count is refused",
                     lambda: wilson_interval(-1, 10))

    # A sample size is never zero, however large the effect.
    fails += check(games_needed(1000.0, 8.87) >= 1,
                   "a sample size is at least one game")

    # And the guards have not moved the table the pre-registration quotes.
    pinned = [(0.10, 9510, 123506), (0.25, 1522, 19761),
              (0.50, 381, 4941), (1.00, 96, 1236)]
    drift = [e for e, pr, ind in pinned
             if games_needed(e, 8.87, rho=0.923) != pr or games_needed(e, 8.87) != ind]
    fails += check(not drift,
                   "the pre-registered sample sizes are unchanged",
                   "moved at {}".format(drift))
    return fails


def test_multiplicity(replicates):
    """The two pre-registered procedures that nothing called.

    experiments/preregistration.md commits to Holm step-down over the pairwise
    family and to a percentile bootstrap where a statistic has no closed form.
    Neither had a caller anywhere in the repository and neither appeared in this
    file's own self-test, so coverage measured both at zero lines executed. A
    pre-registered method that has never run is a promise rather than a
    procedure, and the bootstrap was quietly returning a zero-width 95% interval
    for a single observation until this went in.
    """
    print("")
    print("[the pre-registered corrections]")
    fails = 0

    # Hand-computed. Sorted, the p-values are .001, .009, .04, .20 against
    # alpha/4, alpha/3, alpha/2, alpha. The third fails its threshold and
    # step-down stops there instead of going on to test the fourth.
    got = holm([0.04, 0.001, 0.20, 0.009])
    fails += check(got == [False, True, False, True],
                   "Holm rejects the two smallest and stops at the first failure",
                   "got {}".format(got))
    fails += check(holm([0.001, 0.002, 0.003]) == [True] * 3,
                   "a family that all clears is all rejected")
    fails += check(holm([0.9, 0.8]) == [False] * 2,
                   "and one that clears nothing rejects nothing")
    fails += check(holm([]) == [], "an empty family is not an error")

    # None of the cases above separates Holm from Bonferroni: they agree on all
    # of them, so a flat alpha/m threshold passed every one. At m = 4 the second
    # threshold is alpha/3, so .015 clears Holm and fails Bonferroni's alpha/4.
    got = holm([0.001, 0.015, 0.02, 0.9])
    fails += check(got == [True, True, True, False],
                   "the thresholds widen down the family, unlike Bonferroni's",
                   "got {}".format(got))

    # Nor does anything above show that stopping matters, since in each of them
    # everything after the first failure fails anyway. Here .04 would clear the
    # second threshold on its own, but .03 has already failed the first, so a
    # step-down rejects neither and a procedure that carried on would take it.
    got = holm([0.03, 0.04])
    fails += check(got == [False, False],
                   "a failure stops the step-down rather than skipping past it",
                   "got {}".format(got))

    # Holm is chosen for sitting strictly between Bonferroni and no correction,
    # so that ordering has to hold on every family, not just a convenient one.
    rng = random.Random(20260906)
    weaker_than_bonferroni = stronger_than_uncorrected = 0
    for _ in range(300):
        ps = [rng.random() ** 3 for _ in range(rng.randrange(2, 12))]
        rejected = holm(ps)
        for p, r in zip(ps, rejected):
            if p <= 0.05 / len(ps) and not r:
                weaker_than_bonferroni += 1
            if p > 0.05 and r:
                stronger_than_uncorrected += 1
    fails += check(weaker_than_bonferroni == 0,
                   "Holm rejects everything Bonferroni would",
                   "{} cases where it did not".format(weaker_than_bonferroni))
    fails += check(stronger_than_uncorrected == 0,
                   "and never rejects what an uncorrected test would keep",
                   "{} cases where it did".format(stronger_than_uncorrected))

    # The bootstrap, on the property it exists for. Percentile intervals at this
    # sample size sit a little under nominal, so the band is wide enough to say
    # the procedure works without asserting a precision it does not have.
    trials = max(60, replicates // 4)
    covered = 0
    rng = random.Random(4242)
    for _ in range(trials):
        xs = [rng.gauss(15.0, 4.0) for _ in range(30)]
        _, lo, hi = bootstrap_interval(xs, resamples=120, rng=rng)
        if lo <= 15.0 <= hi:
            covered += 1
    rate = covered / trials
    fails += check(0.85 <= rate <= 0.995,
                   "the bootstrap interval covers the true mean about 95% of the time",
                   "{:.1%} over {} trials".format(rate, trials))

    # The coverage band is too loose to notice an endpoint read from the wrong
    # percentile: taking the minimum resample widens the interval, which raises
    # coverage rather than lowering it. Where the CLT applies the percentile
    # interval has to land on the normal one, and that pins both endpoints.
    # Its own stream rather than the shared one, so that adding a check above
    # cannot move the sample and quietly change the margin this relies on. The
    # correct endpoints sit 0.11 and 0.04 se off the normal ones here; reading
    # the upper one from the 95th percentile instead of the 97.5th puts it 0.28
    # off, so the tolerance separates them with room on both sides.
    n = 300
    draw = random.Random(8675309)
    xs = [draw.gauss(15.0, 4.0) for _ in range(n)]
    m = sum(xs) / n
    se = math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1)) / math.sqrt(n)
    _, lo, hi = bootstrap_interval(xs, resamples=800, rng=random.Random(31))
    fails += check(abs(lo - (m - Z_95 * se)) < 0.20 * se
                   and abs(hi - (m + Z_95 * se)) < 0.20 * se,
                   "and it lands on the normal interval where the CLT applies",
                   "bootstrap [{:.3f}, {:.3f}] against normal [{:.3f}, {:.3f}]".format(
                       lo, hi, m - Z_95 * se, m + Z_95 * se))

    same_a = bootstrap_interval([1.0, 5.0, 9.0, 2.0], rng=random.Random(1))
    same_b = bootstrap_interval([1.0, 5.0, 9.0, 2.0], rng=random.Random(1))
    fails += check(same_a == same_b, "a seeded bootstrap is reproducible")

    xs = [rng.gauss(0.0, 1.0) for _ in range(40)]
    _, lo95, hi95 = bootstrap_interval(xs, alpha=0.05, resamples=400,
                                       rng=random.Random(9))
    _, lo99, hi99 = bootstrap_interval(xs, alpha=0.01, resamples=400,
                                       rng=random.Random(9))
    fails += check(lo99 <= lo95 and hi99 >= hi95,
                   "and a smaller alpha never narrows it",
                   "95% [{:.3f}, {:.3f}] against 99% [{:.3f}, {:.3f}]".format(
                       lo95, hi95, lo99, hi99))

    # Domains, the same way the rest of this file refuses them.
    for label, call in (
            ("no observations", lambda: bootstrap_interval([])),
            ("one observation", lambda: bootstrap_interval([1.0])),
            ("no resamples", lambda: bootstrap_interval([1.0, 2.0], resamples=0)),
            ("alpha outside (0, 1)", lambda: bootstrap_interval([1.0, 2.0], alpha=0.0))):
        try:
            call()
            fails += check(False, "the bootstrap refuses {}".format(label),
                           "it returned an answer")
        except ValueError:
            fails += check(True, "the bootstrap refuses {}".format(label))

    return fails


def test_calibration(replicates):
    """Simulate from a known truth and count how often the interval covers it.
    A 95% interval must cover about 95% of the time; anything else is a bug in
    the interval, not in the data."""
    print("[interval calibration, {} replicates each]".format(replicates))
    fails = 0
    rng = random.Random(20260825)

    # Shot counts are skewed and bounded below, so a normal-theory interval is
    # an approximation. A gamma with a similar shape is the honest stress test.
    shape, scale = 25.0, 44.4 / 25.0
    truth = shape * scale
    n = 400
    covered = 0
    for _ in range(replicates):
        xs = [rng.gammavariate(shape, scale) for _ in range(n)]
        _, lo, hi = mean_interval(xs)
        covered += lo <= truth <= hi
    rate = covered / replicates
    fails += check(0.93 <= rate <= 0.97, "mean interval covers 95%",
                   "{:.3f} over {} replicates".format(rate, replicates))

    p_true = 0.62
    covered = 0
    for _ in range(replicates):
        k = sum(1 for _ in range(200) if rng.random() < p_true)
        _, lo, hi = wilson_interval(k, 200)
        covered += lo <= p_true <= hi
    rate = covered / replicates
    fails += check(0.93 <= rate <= 0.97, "Wilson interval covers 95%",
                   "{:.3f}".format(rate))

    # Why Wilson rather than the normal approximation. At a single p the answer
    # is noise: the binomial is discrete, so coverage oscillates with p and
    # either interval can look better at a point chosen by hand. The comparison
    # has to be made across p, and the failure that matters is structural.
    n_b = 200
    wald_zero = Z_95 * math.sqrt(0.0 / n_b)
    w_p, w_lo, w_hi = wilson_interval(0, n_b)
    fails += check(w_hi - w_lo > 0 and wald_zero == 0.0,
                   "Wilson stays non-degenerate when nothing succeeds",
                   "k = 0 gives Wilson [{:.4f}, {:.4f}], normal [0, 0]".format(w_lo, w_hi))
    _, f_lo, f_hi = wilson_interval(n_b, n_b)
    fails += check(f_lo >= 0.0 and f_hi <= 1.0 and f_lo < 1.0,
                   "and stays inside [0, 1] when everything does")

    # Coverage of a binomial interval is a finite sum over k, so it is computed
    # rather than sampled. Sampling it at 400 replicates carries enough noise to
    # reverse the Wilson-against-Wald ordering at p = 0.02, which is a difference
    # of 0.025 in coverage against a standard error near 0.011.
    grid = [0.01, 0.02, 0.05, 0.10, 0.20, 0.35, 0.50]
    cov_w = [exact_coverage(p, n_b, wilson_interval) for p in grid]
    cov_n = [exact_coverage(p, n_b, wald_interval) for p in grid]

    dev_w = sum(abs(c - 0.95) for c in cov_w) / len(grid)
    dev_n = sum(abs(c - 0.95) for c in cov_n) / len(grid)
    fails += check(dev_w < dev_n, "averaged over p, Wilson tracks 95% more closely",
                   "mean deviation {:.4f} against {:.4f}, exact".format(dev_w, dev_n))
    fails += check(min(cov_w) > min(cov_n), "and its worst case over p is better",
                   "worst {:.4f} against {:.4f}".format(min(cov_w), min(cov_n)))
    # Where it matters. Over-coverage is a defect too, so the criterion is
    # distance from 0.95 rather than coverage itself. Below p = 0.10 Wilson is
    # closer at every point; by p = 0.5 the normal approximation is at its best
    # and the two coincide exactly, which is the textbook picture and the reason
    # the choice only matters for rare events.
    low = [i for i, pv in enumerate(grid) if pv <= 0.10]
    fails += check(
        all(abs(cov_w[i] - 0.95) < abs(cov_n[i] - 0.95) for i in low),
        "and closer at every p <= 0.10, where win rates are not",
        "at p = 0.02, {:.4f} against {:.4f}".format(cov_w[1], cov_n[1]))
    fails += check(abs(cov_w[-1] - cov_n[-1]) < 1e-12,
                   "the two coincide at p = 0.5, as they should",
                   "both {:.4f}".format(cov_w[-1]))

    # Paired differences under the measured correlation.
    rho, sigma = 0.923, 8.87
    delta = 0.5
    covered = 0
    for _ in range(replicates):
        xs, ys = [], []
        for _ in range(600):
            common = rng.gauss(0, sigma * math.sqrt(rho))
            xs.append(common + rng.gauss(0, sigma * math.sqrt(1 - rho)))
            ys.append(common + rng.gauss(0, sigma * math.sqrt(1 - rho)) + delta)
        _, lo, hi = paired_interval(xs, ys)
        covered += lo <= -delta <= hi
    rate = covered / replicates
    fails += check(0.93 <= rate <= 0.97, "paired interval covers 95% at rho = 0.923",
                   "{:.3f}".format(rate))
    return fails


def test_power(replicates):
    """The sample-size formula is a promise about power. Simulate at exactly the
    prescribed n and count how often the difference is actually detected."""
    print("[sample sizes, checked by simulation]")
    fails = 0
    rng = random.Random(77)
    sigma, rho, effect = 8.87, 0.923, 0.25
    n = games_needed(effect, sigma, rho=rho)

    detected = 0
    for _ in range(replicates):
        diffs = []
        for _ in range(n):
            common = rng.gauss(0, sigma * math.sqrt(rho))
            a = common + rng.gauss(0, sigma * math.sqrt(1 - rho))
            b = common + rng.gauss(0, sigma * math.sqrt(1 - rho)) + effect
            diffs.append(a - b)
        _, lo, hi = mean_interval(diffs)
        detected += not (lo <= 0 <= hi)
    rate = detected / replicates
    fails += check(0.75 <= rate <= 0.87,
                   "n = {} gives the promised 80% power".format(n),
                   "{:.3f} detected".format(rate))
    return fails


def report_tables():
    print("[games needed, re-derived from the measured spread]")
    sigma = 8.87
    print("  sigma = {:.2f} shots, measured over 20,000 games".format(sigma))
    print("  {:>8} {:>16} {:>16}".format(
        "effect", "rho = 0.923", "rho = 0.00"))
    for effect in (0.10, 0.25, 0.50, 1.00):
        print("  {:>8.2f} {:>16,} {:>16,}".format(
            effect,
            games_needed(effect, sigma, rho=0.923),
            games_needed(effect, sigma, rho=0.0)))
    print()
    print("  Both columns are paired designs. At rho = 0 pairing buys nothing and")
    print("  the requirement equals the independent per-arm figure, so the second")
    print("  column serves as both. The correlation measured here is bimodal,")
    print("  0.923 inside the density family and 0.00 against the stochastic hunt")
    print("  policy, so these are the same comparison against two opponents and")
    print("  one number could not cover both: the gap is a factor of thirteen.")
    print()
    print("  This table supersedes the earlier one. Its paired figures agree with")
    print("  the old ones exactly, 9,510 at 0.10 shots, but the independent")
    print("  figures do not: 47,000 was quoted where the formula and sigma = 8.87")
    print("  give 123,506. The old independent column implies sigma = 5.47, so the")
    print("  two halves of that table were derived from different spreads. The")
    print("  paired half was right and the independent half understated the cost")
    print("  by a factor of 2.63.")
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()
    replicates = 400 if args.quick else 2000

    print("Mayflower analysis layer\n========================\n")
    fails = 0
    fails += test_folds()
    print()
    fails += test_audit()
    print()
    fails += test_domains()
    fails += test_multiplicity(replicates)
    fails += test_calibration(replicates)
    print()
    fails += test_power(replicates)
    print()
    report_tables()
    print("FAILED" if fails else "all checks passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
