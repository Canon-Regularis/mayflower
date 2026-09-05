"""The three tools nothing executed, checked on what they produce.

`export_pool`, `selfplay` and `optimal` were each covered only sideways: their
argument handling by test_tool_cli, the pool's contents by test_pool, the
optima by test_exact. None of them was ever run and had its output read.

That matters most for `export_pool`, which writes a shipped artefact, and
`selfplay`, which produces the headline policy numbers the report quotes. Each
tool builds the unranking sampler for the standard instance first, which costs
roughly twenty-five seconds whatever size is asked for, so these run in the
slower gate and at the smallest size that still says something.

    python tests/test_tools_output.py
"""

from __future__ import annotations

import io
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = 77
failures = 0

W = H = 10
LENS = [5, 4, 3, 3, 2]
SHIP_CELLS = sum(LENS)


def check(ok, what, detail=""):
    """Detail only on failure: these read as diagnoses, and printing one under a
    passing line ("43.767 not in [41.8, 45.7]  ok") reads as a contradiction."""
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail and not ok:
        print("      " + detail)
    if not ok:
        failures += 1


def exe(name):
    for candidate in (name + ".exe", name):
        p = os.path.join(ROOT, "build", candidate)
        if os.path.exists(p):
            return p
    return None


def placement_cells(idx, L):
    """The exporter's own indexing, so a disagreement shows up here."""
    hcount = H * (W - L + 1)
    if idx < hcount:
        r, c = divmod(idx, W - L + 1)
        return [r * W + c + k for k in range(L)]
    j = idx - hcount
    c, r = divmod(j, H - L + 1)
    return [(r + k) * W + c for k in range(L)]


def test_export_pool():
    """Run the generator and read what it wrote."""
    print("[export_pool writes a legal pool]")
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "pool.bin")
    wanted = 64
    r = subprocess.run([exe("export_pool"), out, str(wanted), "0xC0FFEE"],
                       capture_output=True, text=True, timeout=600)
    check(r.returncode == 0, "the generator succeeds", r.stderr.strip()[:70])
    if not os.path.exists(out):
        check(False, "it wrote a pool at all")
        return

    raw = io.open(out, "rb").read()
    check(len(raw) == wanted * len(LENS),
          "the file is exactly the boards asked for",
          "{} bytes against {} x {}".format(len(raw), wanted, len(LENS)))

    illegal, overlapping, wrong_size = 0, 0, 0
    occ = [0] * (W * H)
    for b in range(len(raw) // len(LENS)):
        used = set()
        base = b * len(LENS)
        for j, L in enumerate(LENS):
            idx = raw[base + j]
            slots = H * (W - L + 1) + (W * (H - L + 1) if L > 1 else 0)
            if idx >= slots:
                illegal += 1
                break
            cells = placement_cells(idx, L)
            if used & set(cells):
                overlapping += 1
                break
            used.update(cells)
        else:
            if len(used) != SHIP_CELLS:
                wrong_size += 1
            for c in used:
                occ[c] += 1

    check(illegal == 0, "every placement index is inside its own table",
          "{} out of range".format(illegal))
    check(overlapping == 0, "no board overlaps its own ships",
          "{} overlapping".format(overlapping))
    check(wrong_size == 0, "every board covers exactly {} cells".format(SHIP_CELLS),
          "{} do not".format(wrong_size))
    check(sum(occ) == wanted * SHIP_CELLS,
          "occupancy sums to the ship-cell count per board",
          "{} against {}".format(sum(occ), wanted * SHIP_CELLS))

    # The same key must give the same pool, or the pool is not reproducible and
    # nothing downstream that quotes it can be replayed.
    again = os.path.join(tmp, "pool2.bin")
    subprocess.run([exe("export_pool"), again, str(wanted), "0xC0FFEE"],
                   capture_output=True, text=True, timeout=600)
    check(os.path.exists(again) and io.open(again, "rb").read() == raw,
          "the same key reproduces the same pool byte for byte")

    other = os.path.join(tmp, "pool3.bin")
    subprocess.run([exe("export_pool"), other, str(wanted), "0xBEEF"],
                   capture_output=True, text=True, timeout=600)
    check(os.path.exists(other) and io.open(other, "rb").read() != raw,
          "and a different key gives a different pool")


def test_selfplay():
    """Run it and hold its summary to what the numbers must satisfy."""
    print("\n[selfplay reports a consistent summary]")
    games = 60
    r = subprocess.run([exe("selfplay"), str(games), "0", "train"],
                       capture_output=True, text=True, timeout=900)
    check(r.returncode == 0, "the run succeeds", r.stderr.strip()[:70])
    text = r.stdout

    check("fold         train" in text, "it reports the fold it drew from")
    drew = re.search(r"drew (\d+) boards from the train fold", text)
    check(drew is not None and int(drew.group(1)) == games,
          "it drew the number of boards asked for",
          drew.group(0) if drew else "(no draw line)")

    # policy  mean  sd  [lo, hi]  median  p95  best  worst  us/game
    rows = re.findall(
        r"^(\S[\S ]*?)\s{2,}([\d.]+)\s+([\d.]+)\s+\[\s*([\d.]+),\s*([\d.]+)\]\s+"
        r"(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", text, re.M)
    check(len(rows) >= 3, "every policy is summarised",
          "{} rows parsed".format(len(rows)))

    for name, mean, sd, lo, hi, median, p95, best, worst in rows:
        mean, sd, lo, hi = float(mean), float(sd), float(lo), float(hi)
        median, p95, best, worst = int(median), int(p95), int(best), int(worst)
        label = name.strip()
        check(lo <= mean <= hi, "{}: the interval contains the mean".format(label),
              "{} not in [{}, {}]".format(mean, lo, hi))
        check(best <= median <= worst, "{}: the median lies inside the range".format(label),
              "{} outside [{}, {}]".format(median, best, worst))
        check(best <= p95 <= worst, "{}: p95 lies inside the range".format(label))
        check(best >= SHIP_CELLS, "{}: no game finishes below the coverage bound".format(label),
              "best {} against {}".format(best, SHIP_CELLS))
        check(worst <= W * H, "{}: no game exceeds the board".format(label),
              "worst {} against {}".format(worst, W * H))
        check(best <= mean <= worst, "{}: the mean lies inside the range".format(label))
        check(sd >= 0.0, "{}: the spread is non-negative".format(label))

    # The paired table divides by spreads that can be zero. density(b=50) and
    # density(b=200) saturate to the same play, so their difference has no
    # spread and the variance ratio printed "CRN saves infx"; a policy with no
    # spread of its own sends rho the same way as 0/0. run_headline copies this
    # table verbatim into the headline record, so a malformed figure here is
    # what the pre-registered run would have preserved.
    saves = re.findall(r"CRN saves (\S+)", text)
    check(saves, "the paired table reports a CRN saving", "none found")
    bad = [v for v in saves if not re.fullmatch(r"\d+(?:\.\d+)?x|unbounded", v)]
    check(not bad, "every CRN saving is a number or a stated non-number",
          "got {}".format(sorted(set(bad))))

    rhos = re.findall(r"rho\s+(\S+)", text)
    check(rhos, "the paired table reports a correlation", "none found")
    bad = [v for v in rhos if not re.fullmatch(r"-?\d+\.\d+", v)]
    check(not bad, "every correlation is a number", "got {}".format(sorted(set(bad))))

    # run_headline turns this same text into the recorded result, and its parser
    # lives in a file no test ran. Checking it here costs nothing extra and ties
    # the captured rows in test_run_headline to the format selfplay emits today.
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import run_headline
    parsed = run_headline.parse(text)
    check(len(parsed["policies"]) == len(rows),
          "the headline parser reads the same policies this test did",
          "{} against {}".format(len(parsed["policies"]), len(rows)))
    check(len(parsed["paired"]) == len(saves),
          "and every paired row it will record",
          "{} against {}".format(len(parsed["paired"]), len(saves)))


def test_optimal_pruning():
    """The pruning ladder, whose last column is the claim that matters."""
    print("\n[optimal's pruning ladder agrees with itself]")
    r = subprocess.run([exe("optimal"), "pruning"], capture_output=True, text=True,
                       timeout=900)
    check(r.returncode == 0, "the ladder runs", r.stderr.strip()[:70])
    text = r.stdout

    verdicts = re.findall(r"^\s*\S+ \{[^}]*\}\s+\d+\s+[\d.]+.*?\s(yes|no|-)\s*$", text, re.M)
    check(verdicts, "the agreement column is printed",
          "{} rows".format(len(verdicts)))
    check("no" not in verdicts,
          "no pruning level disagrees with another",
          "verdicts: {}".format(" ".join(verdicts)))

    # The optima it prints are the ones test_exact pins, so the tool and the
    # test cannot drift apart unnoticed.
    pinned = {"3x3 {2}": 4.5, "4x3 {2}": 5.117647, "4x4 {2}": 6.083333,
              "4x4 {3}": 5.625}
    for inst, want in pinned.items():
        m = re.search(re.escape(inst) + r"\s+\d+\s+([\d.]+)", text)
        check(m is not None and abs(float(m.group(1)) - want) < 1e-5,
              "{} prints its pinned optimum".format(inst),
              "got {}".format(m.group(1) if m else "nothing"))


def main():
    print("the tools nothing ran")
    print("=====================")
    missing = [t for t in ("export_pool", "selfplay", "optimal") if exe(t) is None]
    if missing:
        print("  not built: {}".format(", ".join(missing)))
        return SKIP

    test_export_pool()
    test_selfplay()
    test_optimal_pruning()

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
