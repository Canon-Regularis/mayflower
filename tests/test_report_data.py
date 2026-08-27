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


def main():
    print("the figure-data contract")
    print("========================")
    if not os.path.exists(FIGURES):
        # Not a pass and not a failure. out/ is generated and gitignored, so a
        # clean clone has nothing to check; ctest reports this as Skipped, which
        # stays visible instead of turning green on an empty run.
        print("  out/figures.json is missing; run tools/report_data first")
        return SKIP

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

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
