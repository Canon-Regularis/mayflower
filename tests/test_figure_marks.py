"""The marks a figure draws, against the data it claims to draw.

Every check that existed compared the PROSE to the data. tests/test_report_data
pins the numbers in the sentences, collect_results cross-checks the tools
against each other, and test_provenance ties the page to the engine that built
it. Nothing read the figures. So two of them were wrong for as long as they had
existed and a green suite said nothing:

  - the bound ladder fixed its domain at 12 to 50 and skipped any policy past
    it, so random at 95.40 and parity at 51.60 were dropped. The page drew one
    point under a caption reading "measured policies as points", plural.
  - the survival curves appended the running total before subtracting the
    shot's finishers, plotting P(T >= n) under a title saying P(T > n). The
    random curve ended at 0.1685 on shot 100, the games that finish on exactly
    that shot, when nothing can still be running after 100 shots on a 100-cell
    board.

Both are the same failure as every other one this project has found: a value
computed correctly, published, and read by nothing that could disagree with it.
This is the reader for the marks.

It inverts the coordinate mappings out of the rendered page and compares what it
recovers against out/figures.json, which is what a human did to find the two
above. Narrow on purpose: it checks the properties that were wrong and a few
that could go wrong the same way, rather than trying to re-derive every figure.

    python tests/test_figure_marks.py
"""

from __future__ import annotations

import io
import json
import os
import re
import sys
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT, SKIP, check, report  # noqa: E402

FIGURES = os.path.join(ROOT, "out", "figures.json")
REPORT = os.path.join(ROOT, "out", "report.html")


def figure(html: str, aria_fragment: str) -> str | None:
    """The one svg whose aria-label contains this fragment."""
    i = html.find(aria_fragment)
    if i < 0:
        return None
    start = html.rfind("<svg", 0, i)
    end = html.find("</svg>", i)
    return html[start:end] if start >= 0 and end > 0 else None


def test_ladder_draws_every_policy(html: str, data: dict[str, Any]) -> None:
    """One mark per measured policy, in range or explicitly off the scale."""
    print("\n[the bound ladder]")
    svg = figure(html, "Lower-bound ladder against measured policies")
    if svg is None:
        check(False, "the bound ladder is on the page")
        return

    tips = re.findall(r'class="pt"[^>]*data-tip="([^:]+): ([0-9.]+) \+/- ([0-9.]+)([^"]*)"', svg)
    drawn = {name: (float(mean), rest) for name, mean, _ci, rest in tips}
    want = {p["name"]: p["mean"] for p in data["policies"]}

    check(set(drawn) == set(want),
          "every measured policy has a mark",
          "drawn {}, expected {}".format(sorted(drawn), sorted(want)))
    for name, mean in sorted(want.items()):
        if name not in drawn:
            continue
        check(abs(drawn[name][0] - mean) < 5e-4,
              "the {} mark carries its own mean".format(name),
              "mark says {}, data says {}".format(drawn[name][0], mean))

    # A policy past the axis must say so rather than vanish, and one inside it
    # must not claim to be outside.
    ticks = [float(t) for t in re.findall(r'class="tick"[^>]*>([0-9]+)</text>', svg)]
    # An empty list used to skip the loop below and leave the test green. That
    # loop is one of the two checks this file was written for, the ladder
    # quietly drawing one of three measured policies, so losing it has to fail
    # rather than pass.
    check(bool(ticks), "the ladder carries tick labels to read its scale from",
          "no class=\"tick\" text nodes parsed out of the figure")
    if ticks:
        top = max(ticks)
        for name, (mean, rest) in sorted(drawn.items()):
            off = "beyond the axis" in rest
            check(off == (mean > top),
                  "the {} mark is labelled off the scale only if it is".format(name),
                  "mean {}, axis tops at {}, marked off: {}".format(mean, top, off))


def test_survival_reaches_zero(html: str, data: dict[str, Any]) -> None:
    """The curves plot P(T > n), so every one of them ends on the axis."""
    print("\n[the survival curves]")
    svg = figure(html, "Fraction of games still unfinished")
    if svg is None:
        check(False, "the survival figure is on the page")
        return

    paths = re.findall(r'<path class="line" d="([^"]+)"', svg)
    check(len(paths) == len(data["policies"]),
          "one curve per policy",
          "{} curves against {} policies".format(len(paths), len(data["policies"])))

    # The last vertex of every curve must sit at the same y as the first
    # vertex of a curve whose value is zero. Recover the mapping from the two
    # gridline extremes rather than restating the emitter's constants.
    ends = []
    for d in paths:
        x, y = (float(v) for v in d.split("L")[-1].split(","))
        ends.append(y)
    check(len(set(round(e, 1) for e in ends)) == 1,
          "every curve ends at the same height",
          "ends at {}".format(sorted(set(round(e, 1) for e in ends))))

    # And that height is the axis: the fraction still running after every shot
    # on the board has been fired is zero for any policy that finishes.
    for p in data["policies"]:
        hist = p["histogram"]
        check(sum(hist[len(hist) - 1:]) >= 0 and sum(hist) > 0,
              "the {} histogram is non-empty".format(p["name"]))
    # Likewise. The sibling check above, that every curve ends at the same
    # height, passes on the buggy rendering, because all the curves were wrong
    # together. This one is the check that caught it, so it cannot be allowed
    # to disappear with the regex that feeds it.
    axis = re.search(r'<line class="axis"[^>]*y1="([0-9.]+)"', svg)
    check(axis is not None and bool(ends),
          "the survival figure carries an axis line to measure against",
          "axis found: {}, curve ends found: {}".format(axis is not None, len(ends)))
    if axis and ends:
        check(abs(ends[0] - float(axis.group(1))) < 1.0,
              "and that height is the axis, so nothing is left running",
              "curves end at y={:.1f}, axis at y={}".format(ends[0], axis.group(1)))


def test_paired_boards_share_a_scale(html: str) -> None:
    """Two boards a caption asks a reader to compare, on one ramp.

    Each board direct-labels its own ramp ends, so a stretched pair is
    disclosed rather than hidden, but the prose says the maps "compare their
    rules directly" and two nearly equal numbers took opposite ends of the
    ramp before this. blocking_boards already fixes its scale across four
    boards for the same reason.
    """
    print("\n[the paired boards]")
    for what in ("Mean turn at which each cell is shot",
                 "Fraction of games in which each cell is shot"):
        labels = []
        pos = 0
        while True:
            i = html.find(what, pos)
            if i < 0:
                break
            start = html.rfind("<svg", 0, i)
            end = html.find("</svg>", i)
            svg = html[start:end]
            ends = re.findall(r'class="tick" x="[0-9.]+" y="[0-9.]+" text-anchor="start"[^>]*>'
                              r'([0-9.]+)</text>', svg)
            if ends:
                labels.append((ends[0], ends[1] if len(ends) > 1 else None))
            pos = end if end > 0 else i + 1
        check(len(labels) == 2 and labels[0] == labels[1],
              "the two {} boards share a ramp domain".format(what.split()[0].lower()),
              "ramp ends {}".format(labels))


def main() -> int:
    print("what the figures draw, against what the data says")
    print("=================================================")
    if not os.path.exists(FIGURES) or not os.path.exists(REPORT):
        print("out/figures.json or out/report.html is missing; run the report pipeline")
        return SKIP

    data = json.load(io.open(FIGURES, encoding="utf-8"))
    html = io.open(REPORT, encoding="utf-8").read()

    test_ladder_draws_every_policy(html, data)
    test_survival_reaches_zero(html, data)
    test_paired_boards_share_a_scale(html)
    return report()


if __name__ == "__main__":
    sys.exit(main())
