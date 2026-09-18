"""The JavaScript engine, checked against the Python oracle.

web/engine.js is a second implementation of the counting sweep, written so the
report's live widget can compute a real posterior in the browser. The README has
described it as verified against the C++, but nothing ran that verification: the
JS was checked by hand once and never again. A drift there would show the reader
of the published report wrong probabilities, silently, with the page still
looking exactly right.

This checks it against python/oracle.py, which enumerates boards literally and
shares no code with either the JS or the C++. Three implementations, and the odd
one out would be visible.

Both the plain cell filter and the ordered history gate are covered, because the
gate is where sunk semantics live and that is the part most likely to be wrong.

    python tests/test_engine_js.py
"""

from __future__ import annotations

import io
import json
import os
import subprocess
import sys
from collections.abc import Sequence
from typing import Any
import random

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT  # noqa: E402
from _jsdriver import run_js  # noqa: E402

# This file keeps its own counters. They are function local and returned to the
# caller rather than a mutated module global, so this is a different pattern from
# the other ten tests, not a copy of theirs. Converting a 491 line differential
# test to the shared counter carries more risk than the change is worth. Only the
# column width is aligned, so the suite prints a consistent format.
sys.path.insert(0, os.path.join(ROOT, "python"))

import oracle  # noqa: E402

FREE, EMPTY, OCCUPIED = 0, 1, 2

# Distributions disagree about whether the binary is node or nodejs, so the
# build hands over the one it found.
NODE = os.environ.get("MF_NODE", "node")

# Small enough for literal enumeration, varied enough to exercise both
# orientations, repeated lengths and a non-square board.
CASES = [
    (4, 4, [3, 2]),
    (4, 4, [2, 2]),
    (5, 4, [3, 2]),
    (5, 5, [3, 2, 2]),
    (4, 6, [3, 2]),
    # A one-cell ship has one placement, not two. The JS emitted both, as four of
    # the five C++ sweeps did, and no case here reached it.
    (4, 4, [1, 1]),
    (4, 4, [3, 1]),
]

def occupancy(board: oracle.Board) -> set[oracle.Cell]:
    return {c for ship in board for c in ship}


def count_cells(boards: Sequence[oracle.Board], width: int,
                cells: Sequence[int]) -> int:
    """Boards agreeing with a per-cell filter, by literal enumeration."""
    n = 0
    for b in boards:
        occ = {r * width + c for (r, c) in occupancy(b)}
        ok = True
        for i, v in enumerate(cells):
            if v == EMPTY and i in occ:
                ok = False
                break
            if v == OCCUPIED and i not in occ:
                ok = False
                break
        n += 1 if ok else 0
    return n


def replay(board: oracle.Board, width: int,
           history: Sequence[tuple[int, int, int]]) -> list[tuple[int, int]]:
    """Outcomes this board would give for the history's shot sequence."""
    ships = [set(r * width + c for (r, c) in ship) for ship in board]
    shot = set()
    out = []
    for cell, _, _ in history:
        shot.add(cell)
        hit = next((s for s in ships if cell in s), None)
        if hit is None:
            out.append((MISS_, 0))
        elif hit - shot:
            out.append((HIT_, 0))
        else:
            out.append((SUNK_, len(hit)))
    return out


MISS_, HIT_, SUNK_ = 0, 1, 2


def count_history(boards: Sequence[oracle.Board], width: int,
                  history: Sequence[tuple[int, int, int]]) -> int:
    n = 0
    for b in boards:
        got = replay(b, width, history)
        want = [(o, ln) for (_, o, ln) in history]
        n += 1 if got == want else 0
    return n


# The C++ refuses these outright. A third implementation that answered 0 instead
# would look like it had counted something, and 0 is a legitimate count for a
# fleet that cannot fit, so the two cases were indistinguishable from outside.
VALIDATION_PROBE = r"""
const eng = await import(process.argv[2]);
const bad = [
  ["zero width",        () => eng.makeInstance(0, 4, [2])],
  ["negative width",    () => eng.makeInstance(-4, 4, [2])],
  ["zero height",       () => eng.makeInstance(4, 0, [2])],
  ["empty fleet",       () => eng.makeInstance(4, 4, [])],
  ["zero-length ship",  () => eng.makeInstance(4, 4, [0])],
  ["negative length",   () => eng.makeInstance(4, 4, [-2])],
  ["ship off the board",() => eng.makeInstance(3, 3, [9])],
  ["past 128 cells",    () => eng.makeInstance(20, 20, [2])],
  ["height past 20",    () => eng.makeInstance(4, 30, [2])],
];
const refused = [];
for (const [why, fn] of bad) {
  try { fn(); } catch (e) { refused.push(why); continue; }
  refused.push("ACCEPTED:" + why);
}
// A legal instance must still build, so the guard is not refusing everything.
let legal = false;
try { eng.makeInstance(4, 4, [3, 2]); legal = true; } catch (e) { legal = false; }

// The record, not just the instance. Typed arrays drop an out-of-range write
// rather than throwing, so a shot off the board vanished and constrain returned
// the unconstrained posterior while the caller believed it had conditioned.
const inst = eng.makeInstance(4, 4, [3, 2]);
const unconstrained = (() => {
  const {cells, gate} = eng.constrain(inst, []);
  return eng.count(inst, cells, gate);
})();
const records = [
  ["cell off the board",   [{cell: 999, outcome: eng.MISS}]],
  ["negative cell",        [{cell: -1, outcome: eng.MISS}]],
  ["non-integer cell",     [{cell: 1.5, outcome: eng.MISS}]],
  ["cell shot twice",      [{cell: 0, outcome: eng.MISS}, {cell: 0, outcome: eng.HIT}]],
  ["SUNK with no length",  [{cell: 0, outcome: eng.SUNK}]],
];
const recordsRefused = [];
for (const [why, hist] of records) {
  try {
    const {cells, gate} = eng.constrain(inst, hist);
    eng.count(inst, cells, gate);
    recordsRefused.push("ACCEPTED:" + why);
  } catch (e) { recordsRefused.push(why); }
}
// A legitimate shot must still constrain, and to fewer boards than none.
let constrains = false;
try {
  const {cells, gate} = eng.constrain(inst, [{cell: 0, outcome: eng.MISS}]);
  const n = eng.count(inst, cells, gate);
  constrains = n > 0 && n < unconstrained;
} catch (e) { constrains = false; }

console.log(JSON.stringify({refused, legal, recordsRefused, constrains}));
"""


# (payload, error). Exactly one is set: the probe either parsed a result
# or node failed and said why. It used to return a bare dict on success
# and a pair on failure, so a caller had to know which by looking.
def run_validation_probe() -> tuple[dict[str, Any] | None, str | None]:
    """Returns (list of verdicts, legal-instance-still-builds)."""
    path = os.path.join(ROOT, "out", "_engine_validation.mjs")
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)
    io.open(path, "w", encoding="utf-8", newline="\n").write(VALIDATION_PROBE)
    url = "file:///" + os.path.join(ROOT, "web", "engine.js").replace("\\", "/")
    try:
        proc = subprocess.run([NODE, path, url], capture_output=True, text=True)
    finally:
        if os.path.exists(path):
            os.remove(path)
    if proc.returncode != 0:
        return None, proc.stderr[:300]
    out: dict[str, Any] = json.loads(proc.stdout.strip().splitlines()[-1])
    return out, None


def main() -> int:
    print("javascript engine against the python oracle")
    print("===========================================")
    failures = 0
    jobs: list[dict[str, Any]] = []
    expected: list[Any] = []
    labels: list[str] = []

    rng = random.Random(20260826)

    for (w, h, fleet) in CASES:
        boards = oracle.all_boards(w, h, fleet)

        # Plain cell filters.
        for t in range(6):
            cells = [FREE] * (w * h)
            for i in range(w * h):
                r = rng.randrange(6)
                if r == 0:
                    cells[i] = EMPTY
                elif r == 1:
                    cells[i] = OCCUPIED
            jobs.append({"kind": "cells", "w": w, "h": h, "fleet": fleet, "cells": cells})
            expected.append(count_cells(boards, w, cells))
            labels.append("{}x{} {} cells #{}".format(w, h, fleet, t))

        # Ordered histories, which is where the gate matters.
        for t in range(6):
            truth = boards[rng.randrange(len(boards))]
            order = list(range(w * h))
            rng.shuffle(order)
            shots = order[:min(w * h, 6 + rng.randrange(10))]
            got = replay(truth, w, [(c, 0, 0) for c in shots])
            history = [(shots[i], got[i][0], got[i][1]) for i in range(len(shots))]
            jobs.append({"kind": "history", "w": w, "h": h, "fleet": fleet,
                         "history": [list(x) for x in history]})
            expected.append(count_history(boards, w, history))
            labels.append("{}x{} {} history #{}".format(w, h, fleet, t))

    got = run_js(jobs)
    if got is None:
        print("  could not run node; treating as a failure")
        return 1
    if len(got) != len(expected):
        print("  driver returned {} results, expected {}".format(len(got), len(expected)))
        return 1

    mismatches = 0
    withSunk = 0
    for i, label in enumerate(labels):
        if "history" in label:
            j = jobs[i]
            if any(s[1] == SUNK_ for s in j["history"]):
                withSunk += 1
        if got[i] != expected[i]:
            mismatches += 1
            print("  MISMATCH {}: javascript {}, oracle {}".format(label, got[i],
                                                                   expected[i]))

    print("  {:<58} {}".format(
        "{}/{} cases agree with literal enumeration".format(len(got) - mismatches,
                                                            len(got)),
        "ok" if mismatches == 0 else "FAILED"))
    if mismatches:
        failures += 1
    print("  {:<58} {}".format(
        "{} of the histories carried a SUNK".format(withSunk),
        "ok" if withSunk > 0 else "FAILED"))
    if withSunk == 0:
        failures += 1


    # Instance validation, so the browser engine refuses what the C++ refuses.
    print("[instance validation]")
    probe_out, probe_err = run_validation_probe()
    refused = probe_out.get("refused") if probe_out else None
    legal = probe_out.get("legal") if probe_out else None
    if refused is None:
        print("  {:<58} {}".format("the validation probe runs", "FAILED"))
        print("      " + str(probe_err or legal)[:160])
        failures += 1
    else:
        accepted = [r[len("ACCEPTED:"):] for r in refused if r.startswith("ACCEPTED:")]
        print("  {:<58} {}".format(
            "{} degenerate instances refused".format(len(refused) - len(accepted)),
            "ok" if not accepted else "FAILED"))
        if accepted:
            print("      accepted: " + ", ".join(accepted))
            failures += 1
        print("  {:<58} {}".format(
            "a legal instance still builds", "ok" if legal else "FAILED"))
        if not legal:
            failures += 1

    # The record is validated too, not only the instance.
    if refused is not None and probe_out is not None:
        rec = probe_out.get("recordsRefused", [])
        taken = [r[len("ACCEPTED:"):] for r in rec if r.startswith("ACCEPTED:")]
        print("  {:<58} {}".format(
            "{} malformed records refused".format(len(rec) - len(taken)),
            "ok" if not taken else "FAILED"))
        if taken:
            print("      accepted: " + ", ".join(taken))
            failures += 1
        print("  {:<58} {}".format(
            "a legitimate shot still narrows the posterior",
            "ok" if probe_out.get("constrains") else "FAILED"))
        if not probe_out.get("constrains"):
            failures += 1

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
