"""The figure-data contract, checked against itself.

out/figures.json is the only thing the report is allowed to read, so a mistake
in it is a mistake the reader sees with the page still looking right. Nothing
downstream recomputes an engine number, which is what makes the contract worth
having and also what makes it worth checking.

The belief frames are the part with no other reader. They are the exact cell
marginals after every shot of one recorded game, quantised to a byte, and the
scrubber replays them without running an engine at all. Every invariant the
engine guarantees survives the quantisation and is asserted here: the marginals
sum to the fleet's cell count, a shot cell is certain from the turn it is shot,
the hypothesis count never grows, and the last frame is the hidden board.

The first frame is checked against the prior figure, which a different pass of a
different tool produced, so the two have to agree without either being told.

    python tests/test_report_data.py
"""

from __future__ import annotations

import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIGURES = os.path.join(ROOT, "out", "figures.json")

SHIP_CELLS = 17
MISS = 0

# ctest reads this as "Skipped" via SKIP_RETURN_CODE.
SKIP = 77

failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    if not ok:
        failures += 1


def test_blocking_hover():
    """The blocking boards answer a hover, and answer it with the truth.

    Issue #6: every other board on the page carried a tooltip and these four did
    not, so a reader could count the marks and learn nothing else. The figure now
    reports, per cell, how many placements of that length run through it.

    That number is geometry, so it can be checked rather than trusted: summed
    over a board it must come to length times the number of placements, because
    each placement covers exactly that many cells. It runs before the
    figures.json gate below, needing none of it, so it is checked in every leg
    rather than only where the report has been built.
    """
    BUCKETS_MAX = 12
    print("[the blocking boards]")
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import build_report

    W = H = 10
    for length in (1, 2, 3, 4, 5):
        placements = H * (W - length + 1) + (W * (H - length + 1) if length > 1 else 0)
        total = sum(build_report.placements_through(r, c, length, W, H)
                    for r in range(H) for c in range(W))
        check(total == length * placements,
              "length {}: incidences sum to {} x {} placements".format(
                  length, length, placements),
              "got {} against {}".format(total, length * placements))

    # A 1-cell ship has no second orientation, so it must not be counted twice.
    check(build_report.placements_through(4, 4, 1, W, H) == 1,
          "a one-cell ship covers its own cell once")

    witnesses = [{"length": 3, "beta": 33, "optimal": True,
                  "cells": list(range(33))}]
    svg = build_report.blocking_boards(witnesses, W, H)
    tips = re.findall(r'data-tip="([^"]+)"', svg)
    check(len(tips) == W * H, "every cell of a board carries a tooltip",
          "{} of {}".format(len(tips), W * H))
    check(sum(1 for t in tips if " shot, " in t) == 33,
          "the marked cells are the ones reported as shot",
          "{} reported shot".format(sum(1 for t in tips if " shot, " in t)))
    check(sum(1 for t in tips if "left free" in t) == W * H - 33,
          "and the rest as left free")
    check(all("length-3 placements" in t for t in tips),
          "each names the length its board is drawn for")

    # The numbers the figure actually prints, not just the function behind them.
    # Checking the corner alone was not enough: it meets two placements of any
    # length, so a tooltip reporting a flat 2 for every cell passed that and the
    # sums above, which are computed straight from the function. The rendered
    # incidences have to satisfy the same identity.
    rendered = [int(re.search(r"(?:meets|:) (\d+) of the", t).group(1)) for t in tips]
    check(sum(rendered) == 3 * 160,
          "the incidences the figure prints sum to 3 x 160 as well",
          "got {}".format(sum(rendered)))
    check(len(set(rendered)) > 1,
          "and are not one number repeated across the board",
          "every cell printed {}".format(rendered[0]))

    # The corner is the smallest a cell can be, one along its row and one down
    # its column, and the centre the largest, so between them they pin the
    # clipping at the edge and the count in the middle.
    corner = [t for t in tips if t.startswith("A1 ")]
    centre = [t for t in tips if t.startswith("E5 ")]
    check(len(corner) == 1 and " 2 of the 160 " in corner[0],
          "the corner meets two of the 160 length-3 placements",
          corner[0] if corner else "no A1 tooltip")
    check(len(centre) == 1 and " 6 of the 160 " in centre[0],
          "and the centre meets six",
          centre[0] if centre else "no E5 tooltip")

    graded = re.findall(r'fill="var\(--ramp-(\d+)\)"[^>]*data-tip="([^"]+)"', svg)
    check(len(graded) == W * H, "every cell is graded from the ramp",
          "{} of {}".format(len(graded), W * H))

    rings = re.findall(
        r'<rect x="([0-9.]+)" y="([0-9.]+)"[^>]*stroke="var\(--mark\)" '
        r'stroke-width="2"[^>]*>', svg)
    cell, ox, top = 26, 8, 28
    want = {(str(ox + (i % W) * cell + 3), str(top + (i // W) * cell + 3))
            for i in range(33)}
    check(set(rings) == want,
          "the ring lands on every cell of the set and no other",
          "{} rings, {} of them where no mark is".format(
              len(rings), len(set(rings) - want)))

    # Carried on a surface-coloured stroke underneath, or it would vanish into
    # the dark end of the ramp it is drawn over.
    halo = re.findall(r'stroke="var\(--surface\)" stroke-width="3.5"', svg)
    check(len(halo) == 33, "each ring is carried on a halo so it reads on any tone",
          "{} halos for 33 rings".format(len(halo)))
    check("var(--series-1)" not in svg,
          "and the ring is off the ramp's own hue",
          "the figure still uses series-1, which is ramp-7")

    # Neither the ring nor its halo may take pointer events: they sit on top of
    # the cell, and a marked cell would otherwise be the one cell on the board
    # that answers no hover.
    overlays = re.findall(
        r'<rect [^>]*stroke="var\((?:--mark|--surface)\)"[^>]*>', svg)
    board_overlays = [o for o in overlays
                      if 'stroke-width="2"' in o or 'stroke-width="3.5"' in o]
    check(len(board_overlays) == 66,
          "the ring and its halo are two overlays per marked cell",
          "{} overlays for 33 cells".format(len(board_overlays)))
    check(all('pointer-events="none"' in o for o in board_overlays),
          "and neither of them intercepts the hover",
          "{} take pointer events".format(
              sum(1 for o in board_overlays if 'pointer-events="none"' not in o)))

    css = io.open(os.path.join(ROOT, "tools", "render_report.py"),
                  encoding="utf-8").read()
    themes = re.findall(r"--mark:\s*(#[0-9a-fA-F]{6})", css)
    check(len(themes) == 2, "the mark colour is defined for both themes",
          "found {}".format(themes))

    # The key, so neither encoding needs a hover to be understood.
    check("placements through a cell" in svg and "in the blocking set" in svg,
          "the figure carries a key for both encodings")
    # Every step of the ramp, so the key shows the scale it is a key for. The
    # outlined example beside it is drawn the same size and would be counted
    # too, which is why this asks which buckets appear and not how many swatches.
    swatches = {int(b) for b in re.findall(
        r'height="13" rx="2" fill="var\(--ramp-(\d+)\)"', svg)}
    check(swatches == set(range(BUCKETS_MAX + 1)),
          "whose ramp shows every step of the scale",
          "buckets in the key: {}".format(sorted(swatches)))

    pairs = sorted((int(re.search(r"(?:meets|:) (\d+) of the", t).group(1)), int(b))
                   for b, t in graded)
    check(all(pairs[i][1] <= pairs[i + 1][1] for i in range(len(pairs) - 1)),
          "a cell met by more placements is never shaded lighter",
          "buckets out of order: {}".format(pairs[:6]))
    check(len({b for _, b in pairs}) > 1,
          "and the board is not one flat tone",
          "every free cell shaded {}".format(pairs[0][1]))
    check(pairs[0] == (2, 0), "the floor of the scale is a corner at two placements",
          "lightest free cell is {}".format(pairs[0]))

    five = build_report.blocking_boards(
        [{"length": 5, "beta": 20, "optimal": True, "cells": list(range(20))}], W, H)

    def tones(svg_text):
        out = {}
        for bucket, tip in re.findall(
                r'fill="var\(--ramp-(\d+)\)" data-tip="([^"]+)"', svg_text):
            n = int(re.search(r"(?:meets|:) (\d+) of the", tip).group(1))
            out.setdefault(n, set()).add(int(bucket))
        return out

    t3, t5 = tones(svg), tones(five)
    shared = sorted(set(t3) & set(t5))
    disagree = [n for n in shared if t3[n] != t5[n]]
    check(len(shared) >= 3 and not disagree,
          "a count shades the same on the length-3 and length-5 boards",
          "{} counts in common, disagreeing on {}".format(
              len(shared), [(n, sorted(t3[n]), sorted(t5[n])) for n in disagree[:2]]))
    check(max(b for bs in t5.values() for b in bs) == BUCKETS_MAX,
          "and the length-5 board reaches the top of the scale",
          "highest bucket {}".format(max(b for bs in t5.values() for b in bs)))


def main():
    print("the figure-data contract")
    print("========================")
    test_blocking_hover()

    if not os.path.exists(FIGURES):
        # Not a pass and not a failure. out/ is generated and gitignored, so a
        # clean clone has nothing to check; ctest reports this as Skipped, which
        # stays visible instead of turning green on an empty run.
        print("  out/figures.json is missing; run tools/report_data first")
        # The blocking checks above need none of it, so a failure in them is a
        # failure even here. Returning SKIP unconditionally would have buried
        # them in exactly the legs where out/ is absent, which is every per-push
        # leg, and ctest would have reported the whole thing green.
        return 1 if failures else SKIP

    fig = json.load(io.open(FIGURES, encoding="utf-8"))
    prior = fig["prior"]
    total = prior["total"]
    counts = prior["counts"]
    n = prior["width"] * prior["height"]

    # The prior itself, exactly. These are integer counts, so the sum of the
    # marginals is a rational and lands on 17 with nothing to round.
    num = sum(counts)
    check(num == SHIP_CELLS * total,
          "the prior marginals sum to exactly {}".format(SHIP_CELLS),
          "{} against {} x {}".format(num, SHIP_CELLS, total))

    game = next((g for g in fig["collapse"] if "frames" in g), None)
    if game is None:
        check(False, "one recorded game carries belief frames")
        return 1

    frames = game["frames"]
    cells = game["cells"]
    outcomes = game["outcomes"]
    omega = game["omega"]
    truth = game["truth"]
    turns = len(omega)

    check(len(frames) == turns * n,
          "{} frames of {} cells, one per turn plus the prior".format(turns, n),
          "{} bytes".format(len(frames)))
    if len(frames) != turns * n:
        return 1

    # Frame 0 is the prior, and the prior figure is the same quantity from a
    # different pass. Byte for byte, or one of the two is wrong.
    want = [int(counts[c] / total * 255.0 + 0.5) for c in range(n)]
    worst = max(abs(frames[c] - want[c]) for c in range(n))
    check(worst == 0, "the first frame is the prior figure byte for byte",
          "largest difference {}".format(worst))

    # A byte per cell rounds by at most half a step, so the sum of a frame can
    # miss 17 by at most n/2 steps. Anything larger is not rounding.
    allowed = n * 0.5 / 255.0
    off = max(abs(sum(frames[t * n:(t + 1) * n]) / 255.0 - SHIP_CELLS)
              for t in range(turns))
    check(off <= allowed,
          "every frame's marginals sum to {}".format(SHIP_CELLS),
          "largest departure {:.4f}, quantisation allows {:.4f}".format(off, allowed))

    # A cell that has been shot has no freedom left, so it reads 0 or 1 from
    # that turn onward and never drifts back.
    wrong = sum(1 for t in range(1, turns) for k in range(t)
                if frames[t * n + cells[k]] != (0 if outcomes[k] == MISS else 255))
    check(wrong == 0, "a shot cell is certain from the turn it is shot",
          "{} cells read something else".format(wrong))

    # Evidence only removes configurations.
    grew = [i for i in range(turns - 1) if omega[i + 1] > omega[i]]
    check(not grew, "the hypothesis count never grows",
          "grew at turn(s) {}".format(grew[:5]))

    check(omega[-1] == 1, "the game ends with one configuration standing",
          "ended at {}".format(omega[-1]))

    last = frames[(turns - 1) * n:turns * n]
    agrees = all((last[c] == 255) == (truth[c] == 1) for c in range(n))
    check(agrees, "the last frame is the hidden board")

    # Misses fired once the record already names the board. The report reads
    # this column as a property of the information objective, which holds only
    # while the other two rules score a cell they are certain of highest. A
    # tie-break change in either of them would break that reading silently.
    obj = fig.get("objectives", [])
    if obj and "maxInfoWaste" in obj[0]:
        dirty = [r["instance"] for r in obj
                 if r["densityWaste"] != 0.0 or r["maxProbWaste"] != 0.0]
        check(not dirty, "density and max-P(hit) never shoot a determined board",
              "waste on {}".format(dirty))

        loose = [r["instance"] for r in obj
                 if not 0.0 <= r["maxInfoWaste"] <= r["maxInfo"] - r["optimal"] + 1e-9]
        check(not loose, "the information rule's waste fits inside its loss",
              "outside on {}".format(loose))

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
