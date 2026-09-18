"""The analysis layer.

Three jobs.

FOLDS. Every board id belongs to one of TRAIN, VAL or TEST, decided by the id
alone. The assignment mirrors include/mayflower/folds.hpp exactly, and
tests/test_folds.cpp pins a vector both must reproduce; if they ever drift, the
harness and the analysis would be reading different data without saying so.

SAMPLE SIZES. How many games a comparison needs, derived from the measured
standard deviations and the measured correlation across common random numbers
rather than from a rule of thumb. The correlation is bimodal here, so a single
global number would be wrong in both directions at once.

CALIBRATION. Interval code is code, and it can be wrong. Every interval this
module produces is checked by simulating from a known ground truth and counting
how often the interval covers it. A 95% interval that covers 91% of the time is
a bug, and it gets caught rather than believed. That checking lives in
python/stats_test.py; see the note at the foot of this file for why it is not
here.
"""

from __future__ import annotations

import argparse
import datetime
import functools
import hashlib
import io
import math
import os
import random
import sys

# --- folds ----------------------------------------------------------------

MASK64 = (1 << 64) - 1
TRAIN_SHARE = 0.60
VAL_SHARE = 0.20


def fold_hash(board_id: int) -> int:
    """splitmix64 with a fixed salt. Mirrors detail::foldHash in folds.hpp."""
    z = (board_id + 0x9E3779B97F4A7C15 + 0x5DEECE66D) & MASK64
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return z ^ (z >> 31)


def fold_fraction(board_id: int) -> float:
    return (fold_hash(board_id) >> 11) / 9007199254740992.0


def fold_of(board_id: int) -> str:
    u = fold_fraction(board_id)
    if u < TRAIN_SHARE:
        return "train"
    if u < TRAIN_SHARE + VAL_SHARE:
        return "val"
    return "test"


# --- intervals ------------------------------------------------------------

# The two-sided normal quantile at 95 percent, to full double precision. Named
# rather than typed because it had seven typed sites across two languages in
# two spellings, 1.959963985 and 1.959964, and both reached the same rendered
# page. Mirrors kZ95 in include/mayflower/constants.hpp and Z_95 in
# tools/report_style.py, and tests/test_stated_counts.py pins the three equal.
#
# Not normal_quantile(0.975), which is Acklam's approximation and lands 1.6e-9
# away. A constant this often typed should be the value, not an estimate of it.
Z_95 = 1.959963984540054


def normal_quantile(p: float) -> float:
    """Acklam's inverse normal CDF. Accurate to about 1.15e-9, which is far
    inside anything that matters for a confidence level."""
    a = [-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
         1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00]
    b = [-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
         6.680131188771972e+01, -1.328068155288572e+01]
    c = [-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
         -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00]
    d = [7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
         3.754408661907416e+00]
    plow, phigh = 0.02425, 1 - 0.02425
    if p < plow:
        q = math.sqrt(-2 * math.log(p))
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / \
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1)
    if p > phigh:
        q = math.sqrt(-2 * math.log(1 - p))
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / \
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1)
    q = p - 0.5
    r = q * q
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q / \
           (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1)


def _betacf(a: float, b: float, x: float) -> float:
    """Continued fraction for the incomplete beta, by the modified Lentz method."""
    tiny = 1e-300
    qab, qap, qam = a + b, a + 1.0, a - 1.0
    c = 1.0
    d = 1.0 - qab * x / qap
    if abs(d) < tiny:
        d = tiny
    d = 1.0 / d
    h = d
    for m in range(1, 301):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < tiny:
            d = tiny
        c = 1.0 + aa / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < tiny:
            d = tiny
        c = 1.0 + aa / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < 3e-16:
            break
    return h


def regularised_beta(a: float, b: float, x: float) -> float:
    """I_x(a, b), the regularised incomplete beta function."""
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0
    front = math.exp(math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b) +
                     a * math.log(x) + b * math.log1p(-x))
    if x < (a + 1.0) / (a + b + 2.0):
        return front * _betacf(a, b, x) / a
    return 1.0 - front * _betacf(b, a, 1.0 - x) / b


@functools.lru_cache(maxsize=512)
def student_t_quantile(p: float, df: int) -> float:
    """Inverse Student-t CDF, by bisection on the exact CDF.

    Cached, because the callers ask the same question over and over. A
    calibration run calls mean_interval once per replicate at one fixed alpha
    and one fixed n, so every call wants the identical quantile, and computing
    it from scratch measured about 5.5 ms a time: six seconds added to the fast
    label's stats test and thirty to a full run. The cache makes the second and
    later calls free. It is keyed on the arguments, so it cannot mask a change
    in either.

    Written out rather than approximated because the whole point of using t over
    z is the small-n tail, which is where a cheap approximation is worst. The CDF
    comes from the regularised incomplete beta, and forty-odd bisection steps on
    a monotone function land inside 1e-10, far inside anything a confidence level
    cares about.
    """
    if not 0.0 < p < 1.0:
        raise ValueError("a quantile needs p in (0, 1); got {!r}".format(p))
    if df < 1:
        raise ValueError("degrees of freedom must be at least 1; got {!r}".format(df))
    if p == 0.5:
        return 0.0
    if p < 0.5:
        return -student_t_quantile(1.0 - p, df)

    def cdf(t: float) -> float:
        x = df / (df + t * t)
        tail = 0.5 * regularised_beta(df / 2.0, 0.5, x)
        return 1.0 - tail if t > 0 else tail

    lo, hi = 0.0, 2.0
    # df = 1 is Cauchy, whose quantiles grow without bound as p approaches 1, so
    # the bracket is found rather than assumed.
    #
    # The ceiling has to refuse rather than saturate. Leaving the loop with
    # cdf(hi) still below p means the root is outside the bracket, and bisection
    # then walks lo up to hi and returns the ceiling as though it were an
    # answer: student_t_quantile(1 - 1e-13, 1) returned 1099511627775.5 against
    # a true 3.183e12. Nothing in this repository asks for a quantile that
    # extreme, which is the reason it went unnoticed rather than a reason to
    # leave it returning a number.
    while cdf(hi) < p and hi < 1e12:
        hi *= 2.0
    if cdf(hi) < p:
        raise ValueError(
            "t quantile for p = {!r} at {} degrees of freedom lies beyond 1e12; "
            "the bracket cannot hold it".format(p, df))
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if cdf(mid) < p:
            lo = mid
        else:
            hi = mid
        if hi - lo < 1e-12 * max(1.0, hi):
            break
    return 0.5 * (lo + hi)


def mean_interval(xs, alpha=0.05):
    """Student-t interval for a mean. Reported by the harness.

    A t interval, not a z one. The variance is estimated from the same sample as
    the mean, so the pivot is t with n-1 degrees of freedom; using a normal
    quantile against an estimated variance understates the width, and understates
    it worst where the sample is smallest. Measured against a standard normal,
    the z version covered 70.0% at n = 2 and 91.9% at n = 10 against a nominal
    95%, and only reached the band near n = 30. The calibration suite exercises
    n = 400 and above, where the two agree to three decimals, which is why this
    sat here uncaught.

    Two observations minimum: the spread divides by n-1, so one sample gave a
    bare ZeroDivisionError and none gave another one line earlier. Neither says
    what a caller did wrong, and paired_interval reaches this with whatever it
    was handed.
    """
    n = len(xs)
    if n < 2:
        raise ValueError(
            "an interval needs at least two observations; got {}".format(n))
    if not 0.0 < alpha < 1.0:
        raise ValueError("alpha must lie in (0, 1); got {!r}".format(alpha))
    m = sum(xs) / n
    var = sum((x - m) ** 2 for x in xs) / (n - 1)
    half = student_t_quantile(1 - alpha / 2, n - 1) * math.sqrt(var / n)
    return m, m - half, m + half


def wilson_interval(successes, n, alpha=0.05):
    """Wilson score interval for a proportion. The plain normal interval is
    badly wrong near 0 and 1, which is exactly where win rates live."""
    if n < 0 or successes < 0:
        raise ValueError("counts must be non-negative; got %d of %d" % (successes, n))
    if successes > n:
        raise ValueError("successes cannot exceed trials; got %d of %d" % (successes, n))
    if n == 0:
        return 0.0, 0.0, 1.0
    z = normal_quantile(1 - alpha / 2)
    p = successes / n
    denom = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / denom
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / denom
    # Staying inside [0, 1] is the point of this interval, and rounding can put
    # an endpoint a few ulp outside. Clamp rather than report a probability of
    # -0.0000, which is what the arithmetic actually produces at k = 0.
    return p, max(0.0, centre - half), min(1.0, centre + half)


def wald_interval(successes, n, alpha=0.05):
    """The textbook normal approximation, here only to be compared against.

    The count guards are wilson_interval's, because nonsense in is nonsense
    out either way: this returned a point estimate of 2.5 for 5 successes in 2
    trials, and negative counts ran straight through. The alpha guard goes
    further than wilson_interval, which still has none. The endpoints themselves
    are deliberately NOT clamped to [0, 1], unlike Wilson's. Escaping the unit
    interval is this function's defining flaw and the reason the comparison in
    the calibration section exists, so hiding it here would erase the finding.
    """
    if n < 0 or successes < 0:
        raise ValueError("counts must be non-negative; got %d of %d" % (successes, n))
    if successes > n:
        raise ValueError("successes cannot exceed trials; got %d of %d" % (successes, n))
    if not 0.0 < alpha < 1.0:
        raise ValueError("alpha must lie in (0, 1); got {!r}".format(alpha))
    if n == 0:
        return 0.0, 0.0, 1.0
    z = normal_quantile(1 - alpha / 2)
    p = successes / n
    half = z * math.sqrt(max(p * (1 - p), 0.0) / n)
    return p, p - half, p + half


def exact_coverage(p_true, n, interval, alpha=0.05):
    """Coverage of a binomial interval, summed rather than sampled.

    The binomial is discrete, so coverage oscillates with p and a simulation
    needs an enormous number of replicates to resolve a difference between two
    intervals at one p. The sum over all n+1 outcomes is exact and costs
    nothing.
    """
    total = 0.0
    for k in range(n + 1):
        _, lo, hi = interval(k, n, alpha)
        if lo <= p_true <= hi:
            total += math.comb(n, k) * (p_true ** k) * ((1 - p_true) ** (n - k))
    return total


def paired_interval(xs, ys, alpha=0.05):
    """Interval for a paired difference. Under common random numbers the board
    difficulty is shared, so the difference carries the variance.

    Lengths must match. zip() would otherwise stop at the shorter sequence and
    return a confident interval computed from a prefix, and unequal arrays in a
    paired comparison mean the pairing itself has gone wrong.
    """
    if len(xs) != len(ys):
        raise ValueError(
            "paired samples must be the same length; got %d and %d" % (len(xs), len(ys)))
    return mean_interval([x - y for x, y in zip(xs, ys)], alpha)


def bootstrap_interval(xs, alpha=0.05, resamples=2000, rng=None):
    """Percentile bootstrap, resampling boards rather than moves.

    The same two-observation minimum mean_interval carries, for a sharper
    reason here. One observation resamples to itself every time, so this
    returned a zero-width 95% interval and called it an interval: false
    precision that reads as certainty rather than as the failure it is. Empty
    input gave a bare ZeroDivisionError and no resamples an IndexError, neither
    of which says what the caller did wrong.
    """
    n = len(xs)
    if n < 2:
        raise ValueError(
            "an interval needs at least two observations; got {}".format(n))
    if resamples < 1:
        raise ValueError(
            "a bootstrap needs at least one resample; got {}".format(resamples))
    if not 0.0 < alpha < 1.0:
        raise ValueError("alpha must lie in (0, 1); got {}".format(alpha))
    rng = rng or random.Random(12345)
    means = []
    for _ in range(resamples):
        means.append(sum(xs[rng.randrange(n)] for _ in range(n)) / n)
    means.sort()
    lo = means[int((alpha / 2) * resamples)]
    hi = means[min(resamples - 1, int((1 - alpha / 2) * resamples))]
    return sum(xs) / n, lo, hi


# --- sample sizes ---------------------------------------------------------

def games_needed(effect, sigma, alpha=0.05, power=0.80, rho=0.0):
    """Games per arm to resolve `effect` shots.

    With common random numbers the paired variance is 2*sigma^2*(1-rho), so the
    correlation is what decides the answer. Measured here, rho is 0.923 inside
    the density family and 0.00 between families, so no single figure is right
    for every comparison and this takes it as an argument.
    """
    if effect <= 0:
        raise ValueError("effect must be positive; got %r" % (effect,))
    if sigma <= 0:
        raise ValueError("sigma must be positive; got %r" % (sigma,))
    if not 0.0 <= rho < 1.0:
        raise ValueError("rho must lie in [0, 1); got %r" % (rho,))
    # Three of the five arguments were checked and two were not, so alpha = 1.5
    # returned a game count and power = 5 raised a domain error from inside the
    # normal quantile that named neither the argument nor the caller's mistake.
    if not 0.0 < alpha < 1.0:
        raise ValueError("alpha must lie in (0, 1); got %r" % (alpha,))
    if not 0.0 < power < 1.0:
        raise ValueError("power must lie in (0, 1); got %r" % (power,))
    z_a = normal_quantile(1 - alpha / 2)
    z_b = normal_quantile(power)
    paired_var = 2 * sigma * sigma * (1 - rho)
    # At least one game: the formula rounds to zero for an effect large against
    # the spread, and zero games resolves nothing.
    return max(1, math.ceil(paired_var * (z_a + z_b) ** 2 / (effect * effect)))


# --- multiplicity ---------------------------------------------------------

def holm(pvalues, alpha=0.05):
    """Holm step-down. Controls the family-wise error rate with no independence
    assumption, which a round robin cannot offer.

    A non-finite p-value is refused rather than sorted. NaN compares false
    against everything, so it lands at an arbitrary rank and the step-down stops
    there: [0.001, nan, 0.02] rejected only the first, where the same list
    without the NaN rejects the first and the third. A hypothesis silently not
    rejected is the failure mode this procedure exists to prevent.

    This project has a natural source of one. density(b=50) and density(b=200)
    choose the same cell on every board in the pool, so their paired difference
    is identically zero and a t statistic on it is 0/0.
    """
    if not 0.0 < alpha < 1.0:
        raise ValueError("alpha must lie in (0, 1); got {!r}".format(alpha))
    for i, p in enumerate(pvalues):
        if not math.isfinite(p):
            raise ValueError(
                "p-value {} is {!r}; a test that did not produce a number cannot "
                "be ranked against ones that did".format(i, p))
        if not 0.0 <= p <= 1.0:
            raise ValueError("p-value {} is {!r}, outside [0, 1]".format(i, p))
    order = sorted(range(len(pvalues)), key=lambda i: pvalues[i])
    out = [False] * len(pvalues)
    for rank, idx in enumerate(order):
        if pvalues[idx] <= alpha / (len(pvalues) - rank):
            out[idx] = True
        else:
            break
    return out

# --- the seal -------------------------------------------------------------
#
# Lives in python/audit.py now. Re-exported because tools/run_headline.py
# resolves these through the module attribute, stats.require_unseal rather
# than a bare name, and tests/test_run_headline.py replaces them the same
# way. Importing them here keeps both working.
from audit import (AUDIT_PATH, GENESIS, HEAD_PATH, audit_entries, is_unsealed,
                   read_head, record, require_unseal, verify_audit, write_head)

# The self test lives in python/stats_test.py, beside python/audit_test.py,
# which is the arrangement audit.py already had. It moved because this module
# is imported in production: tools/run_headline.py does `import stats`, and
# while the driver sat here that pulled 420 lines of simulation loops and their
# replicate counts into a measurement run.
#
# It also removes an inversion. This module imported audit_test at module
# scope, so python/audit_test.py had to defer its own `from stats import check`
# inside the function to keep the two from being a cycle at import time. With
# the driver out, nothing in the library imports a test and the deferral is
# gone.
#
#     python python/stats_test.py            # run everything
#     python python/stats_test.py --quick    # fewer replicates
