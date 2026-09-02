"""The board pool the live widget draws its opening posterior from.

web/pool.bin is 200,000 configurations packed one byte per ship. Three things
read it and none checked it. The widget scans the pool while more than 400
boards survive, which is most of the opening, so a malformed or skewed pool
would put a wrong posterior on the page under the label "exact". `export_pool`
is also not run by ctest, so a regression in the exporter ships unnoticed.

Nothing here imports the engine. The pool is decoded with the formula the
exporter documents and checked against the rules directly, so this fails if the
exporter and the decoder ever disagree.

    python tests/test_pool.py
"""

from __future__ import annotations

import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POOL = os.path.join(ROOT, "web", "pool.bin")
FIGURES = os.path.join(ROOT, "out", "figures.json")

W = H = 10
CELLS = W * H
LENS = [5, 4, 3, 3, 2]
SHIP_CELLS = sum(LENS)

SKIP = 77
failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    if not ok:
        failures += 1


def placement_table(L):
    """Every placement of a length-L ship, indexed the way the exporter writes it.

    horizontal at (row, col) -> row * (W-L+1) + col
    vertical   at (row, col) -> H*(W-L+1) + col * (H-L+1) + row
    """
    hcount = H * (W - L + 1)
    out = [None] * (hcount + W * (H - L + 1))
    for r in range(H):
        for c in range(W - L + 1):
            out[r * (W - L + 1) + c] = tuple(r * W + c + k for k in range(L))
    for c in range(W):
        for r in range(H - L + 1):
            out[hcount + c * (H - L + 1) + r] = tuple((r + k) * W + c for k in range(L))
    return out


def main():
    print("the browser board pool")
    print("======================")
    if not os.path.exists(POOL):
        print("  web/pool.bin is missing; run tools/export_pool")
        return SKIP

    raw = io.open(POOL, "rb").read()
    check(len(raw) % len(LENS) == 0,
          "the file is a whole number of boards",
          "{} bytes over {} ships".format(len(raw), len(LENS)))
    if len(raw) % len(LENS) != 0:
        return 1
    n = len(raw) // len(LENS)
    check(n > 0, "the pool is not empty", "{} boards".format(n))

    tables = [placement_table(L) for L in LENS]

    bad_index = 0
    bad_cellcount = 0
    overlapping = 0
    distinct = set()
    occ = [0] * CELLS

    for b in range(n):
        base = b * len(LENS)
        used = set()
        key = raw[base:base + len(LENS)]
        for j, L in enumerate(LENS):
            idx = raw[base + j]
            table = tables[j]
            if idx >= len(table):
                bad_index += 1
                break
            cells = table[idx]
            if len(cells) != L:
                bad_cellcount += 1
                break
            if used & set(cells):
                overlapping += 1
                break
            used.update(cells)
        else:
            if len(used) != SHIP_CELLS:
                bad_cellcount += 1
            for c in used:
                occ[c] += 1
            distinct.add(bytes(key))

    check(bad_index == 0, "every placement index is inside its own table",
          "{} boards carry an out-of-range index".format(bad_index))
    check(overlapping == 0, "no two ships on a board overlap",
          "{} boards place ships on a shared cell".format(overlapping))
    check(bad_cellcount == 0,
          "every board covers exactly {} cells".format(SHIP_CELLS),
          "{} boards do not".format(bad_cellcount))

    # A pool that repeated one board would pass every check above and still be
    # useless as a posterior.
    check(len(distinct) > n * 0.9,
          "the pool is not degenerate",
          "{} distinct boards of {}".format(len(distinct), n))

    # Occupancy has to sum to the ship-cell count exactly, board by board, so
    # this is arithmetic rather than statistics.
    total = sum(occ)
    check(total == n * SHIP_CELLS,
          "occupancy sums to {} per board".format(SHIP_CELLS),
          "{} against {} x {}".format(total, n, SHIP_CELLS))

    # Against the exact prior, when it has been generated. The sampler is a
    # verified bijection, so a skew here means the exporter, not the sampler.
    if os.path.exists(FIGURES):
        fig = json.load(io.open(FIGURES, encoding="utf-8"))
        prior = fig["prior"]
        exact = [c / prior["total"] for c in prior["counts"]]
        worst, at = 0.0, -1
        for c in range(CELLS):
            d = abs(occ[c] / n - exact[c])
            if d > worst:
                worst, at = d, c
        # 5 sigma at the largest marginal and this sample size.
        allowed = 5.0 * (0.2136 * (1 - 0.2136) / n) ** 0.5
        check(worst <= allowed,
              "every cell sits within five sigma of the exact prior",
              "largest departure {:.5f} at cell {}, allowed {:.5f}".format(worst, at, allowed))
    else:
        print("  out/figures.json absent, skipping the prior comparison")

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
