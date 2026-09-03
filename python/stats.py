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
    """Normal-approximation interval for a mean. Reported by the harness.

    Two observations minimum: the spread divides by n-1, so one sample gave a
    bare ZeroDivisionError and none gave another one line earlier. Neither says
    what a caller did wrong, and paired_interval reaches this with whatever it
    was handed.
    """
    n = len(xs)
    if n < 2:
        raise ValueError(
            "an interval needs at least two observations; got {}".format(n))
    m = sum(xs) / n
    var = sum((x - m) ** 2 for x in xs) / (n - 1)
    half = normal_quantile(1 - alpha / 2) * math.sqrt(var / n)
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
    """The textbook normal approximation, here only to be compared against."""
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
    if effect <= 0:
        raise ValueError("effect must be positive; got %r" % (effect,))
    if sigma <= 0:
        raise ValueError("sigma must be positive; got %r" % (sigma,))
    if not 0.0 <= rho < 1.0:
        raise ValueError("rho must lie in [0, 1); got %r" % (rho,))
    z_a = normal_quantile(1 - alpha / 2)
    z_b = normal_quantile(power)
    paired_var = 2 * sigma * sigma * (1 - rho)
    # At least one game: the formula rounds to zero for an effect large against
    # the spread, and zero games resolves nothing.
    return max(1, math.ceil(paired_var * (z_a + z_b) ** 2 / (effect * effect)))


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
# TEST is sealed, and a seal is worth something only if breaking it leaves a
# mark. The mechanism, and then its limits.
#
# Caught:
#   Editing an entry.        Every line carries the digest of everything before
#                            it, so an edit invalidates every later line.
#   Deleting an interior     Same reason.
#     entry.
#   Deleting or commenting   The head file records how many entries there should
#     out trailing entries.  be and what the last hash is. Removing the tail
#                            leaves a chain that is internally consistent, which
#                            is why the chain alone is not enough, and the head
#                            file is what notices.
#
# Not caught:
#   Anyone with write access to both files can recompute the whole history:
#   the digest takes only public inputs, so there is no key and no proof of
#   authorship. A determined author can rewrite the log and the head together
#   and both will verify.
#
#   The anchor against that is version control, not cryptography. Once the log
#   and its head are committed, rewriting them is a diff someone can see. The
#   chain reduces tampering from "edit one line" to "rewrite the file, the head,
#   and the history that contains them", which is the honest claim. It is not
#   append-only in the tamper-proof sense and this file does not claim to be.
#
# Two attacks the chain alone admits, and the head file is what closes both:
# truncating the last line leaves a prefix that still verifies, and prefixing it
# with "#" does the same while leaving the entry visible in the file.

AUDIT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "experiments", "audit.log")
HEAD_PATH = AUDIT_PATH + ".head"
GENESIS = "0" * 64


def _digest(previous: str, payload: str) -> str:
    return hashlib.sha256((previous + "|" + payload).encode("utf-8")).hexdigest()


def audit_entries(path=None):
    """Every entry as (payload, recorded_hash), in file order."""
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


def _head_path(path):
    return (path or AUDIT_PATH) + ".head"


def read_head(path=None):
    """The expected (count, hash). Absent head means an unanchored log."""
    hp = _head_path(path)
    if not os.path.exists(hp):
        return None
    text = io.open(hp, encoding="utf-8").read().split()
    if len(text) != 2:
        return None
    return int(text[0]), text[1]


def write_head(count, digest, path=None):
    with io.open(_head_path(path), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("{} {}\n".format(count, digest))


def verify_audit(path=None):
    """Recompute the chain and check it against the head.

    Returns (ok, index of the first bad entry), with -1 for a chain that is
    internally fine but whose head disagrees, which is what a truncation looks
    like.
    """
    entries = audit_entries(path)
    previous = GENESIS
    for i, (payload, recorded) in enumerate(entries):
        # The sequence number is inside the payload, so a gap breaks the digest
        # as well as the count.
        expected = _digest(previous, payload)
        if expected != recorded:
            return False, i
        previous = recorded

    head = read_head(path)
    if head is None:
        return len(entries) == 0, -1
    count, digest = head
    if count != len(entries) or digest != previous:
        return False, -1
    return True, -1


def record(event: str, experiment: str, detail: str, path=None, when=None):
    """Append one entry, extend the chain, and move the head."""
    path = path or AUDIT_PATH
    ok, bad = verify_audit(path)
    if not ok:
        raise RuntimeError(
            "audit chain or head is broken at entry {}; refusing to append".format(bad))
    entries = audit_entries(path)
    previous = entries[-1][1] if entries else GENESIS
    stamp = when or datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")
    if "|" in experiment or "|" in event:
        raise ValueError("event and experiment must not contain the field separator")
    # Stripped before it is digested, because the reader strips what it
    # reconstructs. An empty detail would otherwise leave a trailing space in the
    # digested string but not in the parsed one, breaking a chain nobody touched.
    payload = "{:04d} | {} | {} | {} | {}".format(
        len(entries) + 1, stamp, event, experiment, detail).strip()
    digest = _digest(previous, payload)
    with io.open(path, "a", encoding="utf-8", newline="\n") as fh:
        fh.write(payload + " | " + digest + "\n")
    write_head(len(entries) + 1, digest, path)
    return payload


def is_unsealed(experiment: str, path=None) -> bool:
    ok, bad = verify_audit(path)
    if not ok:
        raise RuntimeError("audit chain or head is broken at entry {}".format(bad))
    for payload, _ in audit_entries(path):
        parts = [p.strip() for p in payload.split("|")]
        # seq | timestamp | event | experiment | detail
        if len(parts) >= 4 and parts[2] == "unseal" and parts[3] == experiment:
            return True
    return False


def require_unseal(experiment: str, path=None):
    """Guards TEST-fold data. Raises unless the unseal is already on record."""
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
    saved = io.open(tmp, encoding="utf-8").read()
    lines = saved.split("\n")
    for i, line in enumerate(lines):
        if "design fixed" in line:
            lines[i] = line.replace("design fixed", "design changed")
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    ok, bad = verify_audit(tmp)
    fails += check(not ok, "editing an entry breaks the chain",
                   "first bad entry at index {}".format(bad))

    # Peek-then-erase: record the unseal, read TEST, delete the line. The chain
    # alone verifies afterwards, since nothing follows the tail to contradict it,
    # so this is the case the head file has to catch.
    io.open(tmp, "w", encoding="utf-8", newline="\n").write(saved)
    kept = [l for l in saved.split("\n") if "unseal" not in l]
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(kept))
    ok, _ = verify_audit(tmp)
    fails += check(not ok, "truncating the tail is caught by the head")

    # The same trick with a comment marker, which leaves the entry visible.
    io.open(tmp, "w", encoding="utf-8", newline="\n").write(saved)
    commented = ["# " + l if "unseal" in l else l for l in saved.split("\n")]
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(commented))
    ok, _ = verify_audit(tmp)
    fails += check(not ok, "commenting an entry out is caught too")

    io.open(tmp, "w", encoding="utf-8", newline="\n").write(saved)
    fails += check(verify_audit(tmp)[0], "and the untouched log still verifies")

    # An empty detail leaves the payload ending in the separator and a trailing
    # space. The reader strips what it reconstructs, so a writer that digests the
    # unstripped string breaks a chain nobody tampered with, and the log can then
    # neither be appended to nor read for a seal.
    blank = os.path.join(tempfile.mkdtemp(), "audit.log")
    record("create", "scratch", "seeded", path=blank, when="2026-01-01T00:00:00Z")
    record("note", "scratch", "", path=blank, when="2026-01-02T00:00:00Z")
    ok, bad = verify_audit(blank)
    fails += check(ok, "an empty detail does not break its own chain",
                   "first bad entry at index {}".format(bad))
    try:
        record("unseal", "scratch", "after the blank", path=blank,
               when="2026-01-03T00:00:00Z")
        fails += check(True, "and the log can still be appended to")
    except RuntimeError as exc:
        fails += check(False, "and the log can still be appended to", str(exc))
    try:
        fails += check(is_unsealed("scratch", blank),
                       "and the seal is still readable")
    except RuntimeError as exc:
        fails += check(False, "and the seal is still readable", str(exc))

    # A name carrying the field separator must not be able to forge a match.
    threw = False
    try:
        record("register", "demo | unseal | demo", "smuggled", path=tmp,
               when="2026-01-05T00:00:00Z")
    except ValueError:
        threw = True
    fails += check(threw, "a separator in the experiment name is refused")

    lines = io.open(tmp, encoding="utf-8").read().split("\n")
    for i, line in enumerate(lines):
        if "design fixed" in line:
            lines[i] = line.replace("design fixed", "design changed")
    io.open(tmp, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    ok, bad = verify_audit(tmp)
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
    fails += test_calibration(replicates)
    print()
    fails += test_power(replicates)
    print()
    report_tables()
    print("FAILED" if fails else "all checks passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
