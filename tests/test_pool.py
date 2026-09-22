"""The board pool the live widget draws its opening posterior from.

web/pool.bin is 200,000 configurations packed one byte per ship. The widget
scans the pool while more than 400 boards survive, which is most of the
opening, so a malformed or skewed pool would put a wrong posterior on the page
under the label "exact".

Nothing here imports the engine. The pool is decoded with the formula the
exporter documents and checked against the rules directly, so this fails if the
exporter and the decoder ever disagree. web/live.js does refuse a pool it
cannot decode, but by its length and its first board only; this reads all
200,000.

Eight checks, and they are not equal. Six are structural and hold for any legal
pool: the byte count divides, the pool is not empty, every placement index is
inside its own table, no ship overlaps another, every board covers seventeen
cells, occupancy sums. The seventh is a degeneracy floor, which is weakly
distributional rather than structural: a pool of 200,000 copies of one legal
board passes all six above it and fails that one. Only the eighth holds the
pool against the exact prior, and that is the property the widget's posterior
actually rests on.

The eighth needs out/figures.json, so without it this reports Skipped rather
than passing: a green run over the other seven claims more than it checked, and
that is what it did on every push, in a test no CI job ran with the figure data
present.

The exporter itself is covered elsewhere, on the pr label and nightly rather
than on every push: tests/test_tools_output.py runs export_pool and compares
its output against this committed file, which is the reproducibility check this
one cannot make from the bytes alone.

    python tests/test_pool.py
"""

from __future__ import annotations

import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, SKIP, check, failures, report  # noqa: E402
from _pool import CELLS, LENS, SHIP_CELLS, placement_table  # noqa: E402

POOL = os.path.join(ROOT, "web", "pool.bin")
FIGURES = os.path.join(ROOT, "out", "figures.json")


def main() -> int:
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
        used: set[int] = set()
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
    #
    # This sat behind `if os.path.exists(FIGURES)` with an else that printed a
    # line and let the test pass. Of the seven checks above it, six are
    # structural and one is the degeneracy floor; none of them holds the pool
    # against the exact prior, which is the property the widget's opening
    # posterior rests on. out/ is gitignored, so this dropped on every push,
    # and pool was in no job that generates out/, so it ran nowhere in CI.
    #
    # It is a skip now rather than a silent pass, which is what
    # tests/test_report_data.py does with the same artefact: everything that
    # can run has run, and ctest shows the gap instead of reporting green over
    # it. Two separate mistakes are avoided here and they have different
    # consequences. Returning SKIP unconditionally would hide a failure in the
    # seven checks above, in every leg where out/ is absent, which is every
    # push leg. And `if failures` would test a function object, which is always
    # true, turning every legitimate skip into a failure.
    if not os.path.exists(FIGURES):
        print("  out/figures.json is missing; run tools/report_data first")
        return 1 if failures() else SKIP

    fig = json.load(io.open(FIGURES, encoding="utf-8"))
    prior = fig["prior"]
    exact = [c / prior["total"] for c in prior["counts"]]
    # Each cell against its own sigma, rather than one allowance for all
    # hundred taken at the largest marginal. That allowance was 0.00458, which
    # is five sigma where the marginal is 0.2136 and seven and a half where it
    # is 0.0800, so the corners, where the spread is narrowest, were held to
    # the loosest bound and the sentence below was untrue of them. Per cell it
    # is exactly what it says, and it tightens rather than loosens: the worst
    # departure on the committed pool is 3.20 sigma.
    floor = min(exact)
    check(floor > 0.0,
          "every cell has a positive exact marginal to divide by",
          "the smallest is {:.5f}".format(floor))
    worst, at = 0.0, -1
    for c in range(CELLS):
        sigma = (exact[c] * (1 - exact[c]) / n) ** 0.5
        z = abs(occ[c] / n - exact[c]) / sigma if sigma > 0 else 0.0
        if z > worst:
            worst, at = z, c
    check(worst <= 5.0,
          "every cell sits within five sigma of the exact prior",
          "largest departure {:.2f} sigma at cell {}".format(worst, at))

    return report()


if __name__ == "__main__":
    sys.exit(main())
