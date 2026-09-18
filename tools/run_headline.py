"""Run the pre-registered headline comparison on the TEST fold.

This is the only way TEST gets read. The order is fixed by
experiments/preregistration.md and enforced here:

  1. The unseal must already be recorded in experiments/audit.log. If it is not,
     this refuses and nothing is read.
  2. The sample size comes from the pre-registered table and is passed in, not
     chosen after seeing anything.
  3. selfplay is invoked with the token only this script can supply.
  4. The output is parsed and written to experiments/headline_test.json.

There is no stopping rule to apply because there is no decision to make: the
sample size was fixed before the run and the run happens once.

    python tools/run_headline.py --games 20000
"""

from __future__ import annotations

import argparse
import io
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "python"))

import stats  # noqa: E402

EXPERIMENT = "headline-policy-comparison"


# The full hash with a dirty marker, from tools/_provenance.py.
#
# This used to read `stats.__dict__.get("COMMIT", "")`, and stats has no
# COMMIT, so the field was the empty string on every run that ever wrote this
# file. The default hid it: the one record meant to pin the TEST result to a
# build recorded nothing, and would have gone on doing so silently.
from _provenance import full_commit as commit  # noqa: E402


def parse(text):
    """Pull the per-policy rows and the paired differences out of selfplay."""
    out = {"policies": [], "paired": []}

    # policy  mean  sd  [lo, hi]  median  p95  best  worst  us/game
    row = re.compile(
        r"^(\S+)\s+([\d.]+)\s+([\d.]+)\s+\[\s*([\d.]+),\s*([\d.]+)\]\s+"
        r"(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s*$")
    for line in text.split("\n"):
        m = row.match(line.strip())
        if m:
            out["policies"].append({
                "name": m.group(1), "mean": float(m.group(2)),
                "sd": float(m.group(3)),
                "ci": [float(m.group(4)), float(m.group(5))],
                "median": int(m.group(6)), "p95": int(m.group(7)),
                "best": int(m.group(8)), "worst": int(m.group(9)),
            })

    # Three forms, because an interval is not always computable. When every
    # paired difference is zero the spread is zero and the interval is 0/0,
    # which used to print as [+0.000, +0.000]: a 95% interval of zero width,
    # claiming the difference is known exactly rather than not known at all.
    # experiments/headline_test.json still carries one, for density(b=50)
    # against density(b=200), which pick the same cell on every board in the
    # pool; the TRAIN record has been regenerated.
    #
    # The third form is the case those two do not cover: a pair whose
    # differences are all equal but not zero. That also has no spread and so no
    # interval, but the policies are not identical and selfplay does not say
    # they are.
    #
    # Every form has to be matched rather than merely not matched, or the row
    # vanishes from the record and a reader sees nine comparisons where the tool
    # made ten.
    pair = re.compile(
        r"^(\S+)\s+-\s+(\S+)\s+([+-][\d.]+)\s+\[\s*([+-][\d.]+),\s*([+-][\d.]+)\]"
        r"\s+rho\s+([-\d.]+)")
    same = re.compile(
        r"^(\S+)\s+-\s+(\S+)\s+([+-][\d.]+)\s+\[identical on all (\d+)\]"
        r"\s+rho\s+([-\d.]+)")
    flat = re.compile(
        r"^(\S+)\s+-\s+(\S+)\s+([+-][\d.]+)\s+\[no spread, (\d+) boards\]"
        r"\s+rho\s+([-\d.]+)")
    for line in text.split("\n"):
        m = pair.match(line.strip())
        if m:
            out["paired"].append({
                "a": m.group(1), "b": m.group(2), "difference": float(m.group(3)),
                "ci": [float(m.group(4)), float(m.group(5))],
                "rho": float(m.group(6)),
            })
            continue
        m = same.match(line.strip())
        if m:
            out["paired"].append({
                "a": m.group(1), "b": m.group(2), "difference": float(m.group(3)),
                # null, not [0, 0]. The two policies agreed on every board, which
                # is a stronger and different statement than an interval of
                # width zero, and a reader can tell the two apart.
                "ci": None,
                "identical": int(m.group(4)),
                "rho": float(m.group(5)),
            })
            continue
        m = flat.match(line.strip())
        if m:
            out["paired"].append({
                "a": m.group(1), "b": m.group(2), "difference": float(m.group(3)),
                # Also no interval, and deliberately no "identical" key: the
                # difference is a real constant, it simply has no spread to
                # estimate from.
                "ci": None,
                "boards": int(m.group(4)),
                "rho": float(m.group(5)),
            })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, required=True,
                    help="pre-registered sample size; not chosen after the fact")
    ap.add_argument("--fold", default="test", choices=["train", "test"],
                    help="TRAIN needs no unseal and exists to give TEST a "
                         "like-for-like baseline from the same tool and size")
    args = ap.parse_args()

    # The seal, for TEST only. Nothing below this line reads TEST without it.
    if args.fold == "test":
        try:
            stats.require_unseal(EXPERIMENT)
        except PermissionError as e:
            print("REFUSED:", e)
            return 2
        ok, bad = stats.verify_audit()
        if not ok:
            print("REFUSED: the audit chain does not verify at entry", bad)
            return 2
        print("unseal on record and the chain verifies; reading TEST")

    exe = os.path.join(ROOT, "build", "selfplay.exe")
    if not os.path.exists(exe):
        exe = os.path.join(ROOT, "build", "selfplay")
    cmd = [exe, str(args.games), "x", args.fold]
    if args.fold == "test":
        cmd.append("--unsealed")
    print("running:", " ".join(cmd), "\n")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    print(proc.stdout)
    if proc.returncode != 0:
        print("selfplay failed with", proc.returncode, proc.stderr[:400])
        return 1

    parsed = parse(proc.stdout)
    if not parsed["policies"]:
        print("could not parse selfplay output; refusing to record a partial result")
        return 1

    payload = {
        "experiment": EXPERIMENT,
        "fold": args.fold,
        "games": args.games,
        "preregistered": "experiments/preregistration.md",
        "commit": commit(),
        "primary": "mean shots to clear",
        "secondary": "95th percentile shots",
        "results": parsed,
        "raw": proc.stdout,
    }
    target = os.path.join(ROOT, "experiments",
                          "headline_{}.json".format(args.fold))
    with io.open(target, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(payload, fh, indent=1)
        fh.write("\n")
    print("wrote experiments/headline_{}.json".format(args.fold))

    if args.fold == "test":
        stats.record("result", EXPERIMENT,
                     "TEST read, {} games, {} policies recorded".format(
                         args.games, len(parsed["policies"])))
        print("result recorded in the audit log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
