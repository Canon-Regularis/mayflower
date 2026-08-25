"""The analysis layer, and the checks that keep it honest.

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
a bug, and this is where it gets caught rather than believed.

    python python/stats.py            # run everything
    python python/stats.py --quick    # fewer replicates
"""

from __future__ import annotations

import argparse
import datetime
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


def mean_interval(xs, alpha=0.05):
    """Normal-approximation interval for a mean. Reported by the harness."""
    n = len(xs)
    m = sum(xs) / n
    var = sum((x - m) ** 2 for x in xs) / (n - 1)
    half = normal_quantile(1 - alpha / 2) * math.sqrt(var / n)
    return m, m - half, m + half


def wilson_interval(successes, n, alpha=0.05):
    """Wilson score interval for a proportion. The plain normal interval is
    badly wrong near 0 and 1, which is exactly where win rates live."""
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


def paired_interval(xs, ys, alpha=0.05):
    """Interval for a paired difference. With common random numbers the pairing
    is the whole point, so the difference is what carries the variance."""
    return mean_interval([x - y for x, y in zip(xs, ys)], alpha)


def bootstrap_interval(xs, alpha=0.05, resamples=2000, rng=None):
    """Percentile bootstrap, resampling boards rather than moves."""
    rng = rng or random.Random(12345)
    n = len(xs)
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
    z_a = normal_quantile(1 - alpha / 2)
    z_b = normal_quantile(power)
    paired_var = 2 * sigma * sigma * (1 - rho)
    return math.ceil(paired_var * (z_a + z_b) ** 2 / (effect * effect))


# --- multiplicity ---------------------------------------------------------

def holm(pvalues, alpha=0.05):
    """Holm step-down. Controls the family-wise error rate with no independence
    assumption, which a round robin cannot offer."""
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
# TEST is sealed, and a seal only means something if breaking it leaves a mark.
# The audit log is a hash chain: every line carries the digest of everything
# before it, so an edit or a deletion anywhere invalidates every line after.
# Appending is the only operation that keeps the chain intact.

AUDIT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "experiments", "audit.log")
GENESIS = "0" * 64


def _digest(previous: str, payload: str) -> str:
    return hashlib.sha256((previous + "|" + payload).encode("utf-8")).hexdigest()


def audit_entries(path=None):
    """Every entry as (payload, recorded_hash). Comments and blanks are skipped
    and are not part of the chain, so the file stays readable."""
    path = path or AUDIT_PATH
    if not os.path.exists(path):
        return []
    out = []
    for line in io.open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        payload, _, recorded = line.rpartition(" | ")
        out.append((payload.strip(), recorded.strip()))
    return out


def verify_audit(path=None):
    """Recompute the chain. Returns (ok, index of the first bad entry)."""
    previous = GENESIS
    for i, (payload, recorded) in enumerate(audit_entries(path)):
        expected = _digest(previous, payload)
        if expected != recorded:
            return False, i
        previous = recorded
    return True, -1


def record(event: str, experiment: str, detail: str, path=None, when=None):
    """Append one entry and close the chain over it."""
    path = path or AUDIT_PATH
    ok, bad = verify_audit(path)
    if not ok:
        raise RuntimeError(
            "audit chain is broken at entry {}; refusing to append".format(bad))
    entries = audit_entries(path)
    previous = entries[-1][1] if entries else GENESIS
    stamp = when or datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")
    payload = "{} | {} | {} | {}".format(stamp, event, experiment, detail)
    with io.open(path, "a", encoding="utf-8", newline="\n") as fh:
        fh.write(payload + " | " + _digest(previous, payload) + "\n")
    return payload


def is_unsealed(experiment: str, path=None) -> bool:
    ok, bad = verify_audit(path)
    if not ok:
        raise RuntimeError("audit chain is broken at entry {}".format(bad))
    for payload, _ in audit_entries(path):
        parts = [p.strip() for p in payload.split("|")]
        if len(parts) >= 3 and parts[1] == "unseal" and parts[2] == experiment:
            return True
    return False


def require_unseal(experiment: str, path=None):
    """Guards TEST-fold data. Raises unless the unseal is already on record, so
    the number cannot be read first and justified afterwards."""
    if not is_unsealed(experiment, path):
        raise PermissionError(
            "experiment '{}' has not been unsealed; record the unseal in "
            "experiments/audit.log before reading TEST".format(experiment))


# --- the checks -----------------------------------------------------------

def check(ok, what, detail=""):
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    return 0 if ok else 1


def test_audit():
    """The seal, checked by breaking it."""
    print("[the seal]")
    fails = 0
    import tempfile

    tmp = os.path.join(tempfile.mkdtemp(), "audit.log")
    io.open(tmp, "w", encoding="utf-8", newline="\n").write(
        "# a scratch chain\n")

    record("create", "scratch", "seeded", path=tmp, when="2026-01-01T00:00:00Z")
    record("register", "demo", "design fixed", path=tmp, when="2026-01-02T00:00:00Z")
    ok, _ = verify_audit(tmp)
    fails += check(ok, "a freshly written chain verifies")

    fails += check(not is_unsealed("demo", tmp), "registering is not unsealing")
    try:
        require_unseal("demo", tmp)
        fails += check(False, "reading TEST without an unseal is refused")
    except PermissionError:
        fails += check(True, "reading TEST without an unseal is refused")

    record("unseal", "demo", "pre-registered run", path=tmp, when="2026-01-03T00:00:00Z")
    fails += check(is_unsealed("demo", tmp), "and permitted once the unseal is recorded")

    # Tamper with the middle and the chain must notice.
    lines = io.open(tmp, encoding="utf-8").read().split("\n")
    for i, line in enumerate(lines):
        if "design fixed" in line:
            lines[i] = line.replace("design fixed", "design changed")
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    ok, bad = verify_audit(tmp)
    fails += check(not ok, "editing an entry breaks the chain",
                   "first bad entry at index {}".format(bad))
    try:
        record("unseal", "other", "should fail", path=tmp, when="2026-01-04T00:00:00Z")
        fails += check(False, "and appending to a broken chain is refused")
    except RuntimeError:
        fails += check(True, "and appending to a broken chain is refused")

    # The real log must verify too.
    ok, bad = verify_audit()
    fails += check(ok, "the repository's own audit log verifies",
                   "" if ok else "first bad entry at index {}".format(bad))
    return fails


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
    wald_zero = 1.959963985 * math.sqrt(0.0 / n_b)
    w_p, w_lo, w_hi = wilson_interval(0, n_b)
    fails += check(w_hi - w_lo > 0 and wald_zero == 0.0,
                   "Wilson stays non-degenerate when nothing succeeds",
                   "k = 0 gives Wilson [{:.4f}, {:.4f}], normal [0, 0]".format(w_lo, w_hi))
    _, f_lo, f_hi = wilson_interval(n_b, n_b)
    fails += check(f_lo >= 0.0 and f_hi <= 1.0 and f_lo < 1.0,
                   "and stays inside [0, 1] when everything does")

    grid = [0.01, 0.02, 0.05, 0.10, 0.20, 0.35, 0.50]
    reps = max(200, replicates // 2)
    cov_w, cov_n = [], []
    for p_true in grid:
        cw = cn = 0
        for _ in range(reps):
            k = sum(1 for _ in range(n_b) if rng.random() < p_true)
            _, lo, hi = wilson_interval(k, n_b)
            cw += lo <= p_true <= hi
            p = k / n_b
            half = 1.959963985 * math.sqrt(max(p * (1 - p), 0) / n_b)
            cn += p - half <= p_true <= p + half
        cov_w.append(cw / reps)
        cov_n.append(cn / reps)

    dev_w = sum(abs(c - 0.95) for c in cov_w) / len(grid)
    dev_n = sum(abs(c - 0.95) for c in cov_n) / len(grid)
    fails += check(dev_w < dev_n, "averaged over p, Wilson tracks 95% more closely",
                   "mean deviation {:.4f} against {:.4f}".format(dev_w, dev_n))
    fails += check(min(cov_w) > min(cov_n), "and its worst case over p is better",
                   "worst {:.3f} against {:.3f}, both at p = {:.2f}".format(
                       min(cov_w), min(cov_n), grid[cov_n.index(min(cov_n))]))

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
    fails += test_calibration(replicates)
    print()
    fails += test_power(replicates)
    print()
    report_tables()
    print("FAILED" if fails else "all checks passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
