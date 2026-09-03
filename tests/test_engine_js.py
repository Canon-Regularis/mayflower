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
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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

DRIVER = r"""
import { makeInstance, count, constrain, marginals }
  from './web/engine.js';

const jobs = JSON.parse(process.argv[2]);
const out = [];
for (const j of jobs) {
  const inst = makeInstance(j.w, j.h, j.fleet);
  if (j.kind === 'cells') {
    out.push(Number(count(inst, Int8Array.from(j.cells), null)));
  } else {
    const hist = j.history.map(s => ({ cell: s[0], outcome: s[1], length: s[2] }));
    const { cells, gate } = constrain(inst, hist);
    if (j.kind === 'marginals') {
      const m = marginals(inst, cells, gate);
      const t = Number(m.total);
      out.push(Array.from(m.occ, v => (t > 0 ? Number(v) / t : 0)));
    } else {
      out.push(Number(count(inst, cells, gate)));
    }
  }
}
console.log(JSON.stringify(out));
"""


def run_js(jobs):
    driver = os.path.join(ROOT, "_engine_probe.mjs")
    io.open(driver, "w", encoding="utf-8", newline="\n").write(DRIVER)
    try:
        proc = subprocess.run([NODE, driver, json.dumps(jobs)],
                              cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            print("node failed:", proc.stderr[:600])
            return None
        return json.loads(proc.stdout.strip().split("\n")[-1])
    finally:
        if os.path.exists(driver):
            os.remove(driver)


def occupancy(board):
    return {c for ship in board for c in ship}


def count_cells(boards, width, cells):
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


def replay(board, width, history):
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


def count_history(boards, width, history):
    n = 0
    for b in boards:
        got = replay(b, width, history)
        want = [(o, ln) for (_, o, ln) in history]
        n += 1 if got == want else 0
    return n


# The widget's handoff ------------------------------------------------------
#
# The live widget answers from a 200,000-board sample while the sample is large
# and from the exact sweep once it is spent. The rule has to key on the sample
# alone. An earlier version also forced the sampled branch through the first six
# shots, to keep the expensive opening sweep out of the browser, and that
# inverted the guarantee: an opening that sinks two ships leaves a handful of
# survivors, and the widget drew a posterior quantised to that handful and fired
# on it.
#
# The two regimes are complementary only under the survivor rule, because the
# sample runs out exactly when the record is constraining and a constraining
# record is a cheap sweep. This holds the rule to that.

LENS = [5, 4, 3, 3, 2]


def placement_cells(idx, L, w=10, h=10):
    hcount = h * (w - L + 1)
    if idx < hcount:
        r, c = divmod(idx, w - L + 1)
        return [r * w + c + k for k in range(L)]
    j = idx - hcount
    c, r = divmod(j, h - L + 1)
    return [(r + k) * w + c for k in range(L)]


def pool_boards():
    raw = io.open(os.path.join(ROOT, "web", "pool.bin"), "rb").read()
    n = len(raw) // len(LENS)
    return raw, n


def survivors_for(raw, n, history):
    """Boards of the pool consistent with the record, by the widget's own rule."""
    alive = 0
    for bi in range(n):
        base = bi * len(LENS)
        owner = {}
        for j, L in enumerate(LENS):
            for c in placement_cells(raw[base + j], L):
                owner[c] = j
        rem = list(LENS)
        ok = True
        for cell, outcome, length in history:
            j = owner.get(cell)
            if j is None:
                if outcome != MISS_:
                    ok = False
                    break
                continue
            if outcome == MISS_:
                ok = False
                break
            rem[j] -= 1
            if rem[j] == 0:
                if outcome != SUNK_ or length != LENS[j]:
                    ok = False
                    break
            elif outcome != HIT_:
                ok = False
                break
        alive += 1 if ok else 0
    return alive


def check_widget_handoff():
    print("\nthe live widget's sample-to-exact handoff")
    print("========================================")
    failures = 0
    src = io.open(os.path.join(ROOT, "web", "live.js"), encoding="utf-8").read()

    m = re.search(r"SWITCH_TO_EXACT\s*=\s*(\d+)", src)
    threshold = int(m.group(1)) if m else -1
    m = re.search(r"if \(survivors\.length >= SWITCH_TO_EXACT([^)]*)\) \{", src)
    extra = m.group(1).strip() if m else "MISSING"
    ok = m is not None and extra == ""
    print("  {:<56} {}".format(
        "the handoff keys on the survivor count and nothing else",
        "ok" if ok else "FAILED"))
    if not ok:
        print("      the branch carries an extra clause: {!r}".format(extra))
        failures += 1

    # The rule is only worth guarding if a thin opening is reachable. Sink the
    # 2-ship and a 3-ship of the pool's first board: five shots, no misses.
    raw, n = pool_boards()
    history = []
    for j in (4, 2):
        cs = placement_cells(raw[j], LENS[j])
        for k, c in enumerate(cs):
            last = k == len(cs) - 1
            history.append((c, SUNK_ if last else HIT_, LENS[j] if last else 0))

    alive = survivors_for(raw, n, history)
    print("  {:<56} {}".format(
        "a five-shot opening can leave {} of {:,} alive".format(alive, n),
        "ok" if alive < threshold else "FAILED"))
    if alive >= threshold:
        print("      nothing to guard: the sample never goes thin in the opening")
        failures += 1

    # And the sampled answer really is wrong there, not merely coarse.
    job = [{"kind": "marginals", "w": 10, "h": 10, "fleet": LENS,
            "history": [list(x) for x in history]}]
    got = run_js(job)
    if got is None:
        print("  could not run node; treating as a failure")
        return 1
    exact = got[0]

    counts = [0] * 100
    kept = 0
    for bi in range(n):
        base = bi * len(LENS)
        owner = {}
        for j, L in enumerate(LENS):
            for c in placement_cells(raw[base + j], L):
                owner[c] = j
        rem = list(LENS)
        ok2 = True
        for cell, outcome, length in history:
            j = owner.get(cell)
            if j is None:
                if outcome != MISS_:
                    ok2 = False
                    break
                continue
            if outcome == MISS_:
                ok2 = False
                break
            rem[j] -= 1
            if rem[j] == 0:
                if outcome != SUNK_ or length != LENS[j]:
                    ok2 = False
                    break
            elif outcome != HIT_:
                ok2 = False
                break
        if ok2:
            kept += 1
            for c in owner:
                counts[c] += 1
    sampled = [c / kept for c in counts] if kept else [0.0] * 100
    worst = max(abs(sampled[i] - exact[i]) for i in range(100))
    print("  {:<56} {}".format(
        "the sampled posterior is off by {:.2f} there".format(worst),
        "ok" if worst > 0.10 else "FAILED"))
    if worst <= 0.10:
        print("      the sample happens to agree, so this case proves nothing")
        failures += 1
    return failures



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
console.log(JSON.stringify({refused, legal}));
"""


def run_validation_probe():
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
    out = json.loads(proc.stdout.strip().splitlines()[-1])
    return out["refused"], out["legal"]


def main():
    print("javascript engine against the python oracle")
    print("===========================================")
    failures = 0
    jobs, expected, labels = [], [], []

    import random
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

    print("  {:<56} {}".format(
        "{}/{} cases agree with literal enumeration".format(len(got) - mismatches,
                                                            len(got)),
        "ok" if mismatches == 0 else "FAILED"))
    if mismatches:
        failures += 1
    print("  {:<56} {}".format(
        "{} of the histories carried a SUNK".format(withSunk),
        "ok" if withSunk > 0 else "FAILED"))
    if withSunk == 0:
        failures += 1

    failures += check_widget_handoff()

    # Instance validation, so the browser engine refuses what the C++ refuses.
    print("[instance validation]")
    refused, legal = run_validation_probe()
    if refused is None:
        print("  {:<56} {}".format("the validation probe runs", "FAILED"))
        print("      " + str(legal)[:160])
        failures += 1
    else:
        accepted = [r[len("ACCEPTED:"):] for r in refused if r.startswith("ACCEPTED:")]
        print("  {:<56} {}".format(
            "{} degenerate instances refused".format(len(refused) - len(accepted)),
            "ok" if not accepted else "FAILED"))
        if accepted:
            print("      accepted: " + ", ".join(accepted))
            failures += 1
        print("  {:<56} {}".format(
            "a legal instance still builds", "ok" if legal else "FAILED"))
        if not legal:
            failures += 1

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
