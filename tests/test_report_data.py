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


def test_prose_figures(fig):
    """Numbers written into the prose, held to the data they describe.

    The page's own claim is that nothing on it is typed in, and for the figures
    that is true: they are formatted from figures.json. The prose is not. A
    handful of quantities are written as literals in render_report.py because
    they read better inside a sentence, and nothing checked them, so the sentence
    could go on asserting a ratio the sweep had moved away from.

    The literal is read out of the source and compared to the data rather than
    restated here, since a test that hardcodes both sides checks nothing.
    """
    print("\n[figures written into the prose]")
    src = io.open(os.path.join(ROOT, "tools", "render_report.py"),
                  encoding="utf-8").read()

    # Boards per lattice edge, which is what the counting section is named for.
    m = re.search(r"at (\d+) boards an edge", src)
    ratio = fig["prior"]["total"] / fig["lattice"]["edges"]
    check(m is not None and abs(int(m.group(1)) - ratio) < 1.0,
          "the boards-an-edge heading matches the lattice",
          "prose {}, data {:.1f}".format(m.group(1) if m else "?", ratio))

    # Centre against corner in the prior, quoted twice as a ratio.
    m = re.search(r"([0-9]\.[0-9]{2})[ -]to[ -]1", src)
    counts, total = fig["prior"]["counts"], fig["prior"]["total"]
    got = (counts[44] / total) / (counts[0] / total)
    check(m is not None and abs(float(m.group(1)) - got) < 0.005,
          "the centre-to-corner ratio matches the prior",
          "prose {}, data {:.3f}".format(m.group(1) if m else "?", got))

    # The suboptimality of max-P(hit) on 4x4 {3,2}, quoted as three integers
    # over the same configuration count. This is the one number the report calls
    # exact, so it has to divide out to the gap the objectives sweep reports.
    m = re.search(r"it (?:takes|spends) (\d+) shots? across the space where optimal "
                  r"play (?:takes|spends) (\d+)", src)
    if m is None:
        check(False, "the 4x4 {3,2} shot totals are quoted in the prose")
    else:
        worse, best = int(m.group(1)), int(m.group(2))
        row = next((o for o in fig["objectives"] if o["instance"] == "4x4 {3,2}"), None)
        if row is None:
            check(False, "the objectives sweep still covers 4x4 {3,2}")
        else:
            n = row["configurations"]
            check(abs(worse / n - row["maxProb"]) < 1e-6
                  and abs(best / n - row["optimal"]) < 1e-6,
                  "the 4x4 {3,2} totals divide out to the measured optima",
                  "{}/{} = {:.5f} against {:.5f}, {}/{} = {:.5f} against {:.5f}".format(
                      worse, n, worse / n, row["maxProb"],
                      best, n, best / n, row["optimal"]))
            stated = re.search(r"difference of exactly (\d+)", src)
            check(stated is not None and int(stated.group(1)) == worse - best,
                  "and the difference it calls exact is that difference",
                  "prose {}, {} - {} = {}".format(
                      stated.group(1) if stated else "?", worse, best, worse - best))


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
    test_prose_figures(fig)
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
