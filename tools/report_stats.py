"""The arithmetic the report quotes, kept out of the renderer.

Seventy five lines of statistics lived in tools/render_report.py, four hundred
lines above a paragraph telling the reader that the renderer recomputes nothing.
A rank correlation with midrank tie handling and an ordinary least squares fit
are not formatting helpers.

They stay in the tools layer rather than moving to python/stats.py, because
python/ is the analysis layer and reaches rendering only through the figure data
contract. Sending renderer-specific helpers the other way would cross that line
for no gain.

The four constants below are pinned to the standard instance, which is stated
here rather than left implicit: FREE_PRODUCT and CRUDE_PROFILES encode 10x10
with fleet {5,4,3,3,2}, and LOG2_6 encodes the five ship answer alphabet. A
payload describing a different instance would print them unchanged.
"""

from __future__ import annotations

import math

def _pearson(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    da = math.sqrt(sum((x - ma) ** 2 for x in a))
    db = math.sqrt(sum((y - mb) ** 2 for y in b))
    return num / (da * db) if da and db else 0.0


def _ranks(v):
    """Midranks. The prior takes 15 distinct values over 100 cells, one per
    dihedral orbit, so ordinal ranks would break 85 ties by board index and make
    the coefficient depend on that order."""
    order = sorted(range(len(v)), key=lambda i: v[i])
    out = [0.0] * len(v)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
            j += 1
        for k in range(i, j + 1):
            out[order[k]] = (i + j) / 2.0
        i = j + 1
    return out


def _spearman(a, b):
    return _pearson(_ranks(a), _ranks(b))


def _parity_split(values, width):
    """Mean over the two diagonal colour classes of the board."""
    ev = [v for i, v in enumerate(values) if ((i // width) + (i % width)) % 2 == 0]
    od = [v for i, v in enumerate(values) if ((i // width) + (i % width)) % 2 == 1]
    return sum(ev) / len(ev), sum(od) / len(od)


def _survival(hist):
    total = sum(hist) or 1
    run, out = total, []
    for n in range(len(hist)):
        out.append(run / total)
        run -= hist[n]
    return out


# Placed independently, each ship of length L has 2N(N-L+1) positions on an NxN
# board; the two 3-ships are interchangeable, hence the 2!. The gap between this
# and the true count is what the no-overlap rule costs.
FREE_PRODUCT = 120 * 140 * 160 * 160 // 2 * 180

# The profile carries ten row extensions in 0..4, a vertical run in 0..4, and 24
# fleet-usage states, so 5^10 x 5 x 24.
CRUDE_PROFILES = 5 ** 11 * 24


def _loglog_slope(points):
    """Empirical exponent of |Omega| against board side, with its R^2."""
    xs = [math.log(p["n"]) for p in points]
    ys = [math.log(p["omega"]) for p in points]
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    b1 = sum((a - mx) * (c - my) for a, c in zip(xs, ys)) / sum((a - mx) ** 2 for a in xs)
    b0 = my - b1 * mx
    ss = sum((c - (b0 + b1 * a)) ** 2 for a, c in zip(xs, ys))
    tt = sum((c - my) ** 2 for c in ys)
    return b1, (1 - ss / tt if tt else 0.0)


# The answer alphabet is {MISS, HIT, SUNK(2), ..., SUNK(5)}.
LOG2_6 = math.log2(6)

# Binary entropy at p = 0.9, the worked example of a shot the information
# objective declines.
BIN_H_09 = -(0.9 * math.log2(0.9) + 0.1 * math.log2(0.1))
