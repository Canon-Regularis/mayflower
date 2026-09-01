"""Render the figure-data contract as a self-contained HTML report.

The engine writes out/figures.json; this reads it and emits out/report.html.
Nothing here recomputes an engine number, so a figure cannot disagree with the
engine that produced it.

Every figure is complete inline SVG. The page is correct with JavaScript off;
JavaScript only adds hover readouts.
"""

import html
import io
import json
import math
import os
import sys

# Validated categorical slots (see the palette validator: all six checks pass in
# both modes, worst adjacent CVD dE 9.2 light / 9.4 dark).
SERIES = ["--series-1", "--series-2", "--series-3"]

# Blue sequential ramp, 100 -> 700.
RAMP = ["#cde2fb", "#b7d3f6", "#9ec5f4", "#86b6ef", "#6da7ec", "#5598e7",
        "#3987e5", "#2a78d6", "#256abf", "#1c5cab", "#184f95", "#104281", "#0d366b"]
BUCKETS = 12


def esc(s):
    return html.escape(str(s), quote=True)


def fmt(n, decimals=0):
    if decimals == 0:
        return f"{int(round(n)):,}"
    return f"{n:,.{decimals}f}"


# --------------------------------------------------------------------------- #
# SVG primitives
# --------------------------------------------------------------------------- #

def svg_open(w, h, label):
    """Open a figure at its design size.

    The style caps the rendered width at the width the figure was drawn for, so
    no figure is upscaled and every label on the page keeps the size it was set
    in. Below that width the svg still scales down with its container."""
    return (f'<svg viewBox="0 0 {w} {h}" role="img" aria-label="{esc(label)}" '
            f'preserveAspectRatio="xMidYMid meet" style="max-width:{w}px">')


def axis_line(x1, y1, x2, y2, cls="axis"):
    return f'<line class="{cls}" x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}"/>'


def legend(x, y, entries, gap=136):
    """Swatch plus label, laid out in a row. Present whenever two or more series
    share a plot, alongside the direct labels."""
    out = []
    for i, (name, var) in enumerate(entries):
        cx = x + i * gap
        out.append(f'<rect x="{cx:.1f}" y="{y - 8:.1f}" width="10" height="10" rx="2" '
                   f'fill="var({var})"/>')
        out.append(f'<text class="legendlbl" x="{cx + 15:.1f}" y="{y + 1:.1f}" '
                   f'text-anchor="start">{esc(name)}</text>')
    return "".join(out)


def text(x, y, s, cls="lbl", anchor="middle", extra=""):
    return (f'<text class="{cls}" x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" {extra}>'
            f'{esc(s)}</text>')


# --------------------------------------------------------------------------- #
# Figures
# --------------------------------------------------------------------------- #

def board_heatmap(values, width, height, label, fmt_cell, caption_scale, cell=46):
    """A 10x10 board rendered as the same widget everywhere: row 0 at the top,
    columns A onward, one grid, one glyph vocabulary."""
    pad_l, pad_t = 34, 26
    w = pad_l + width * cell + 12
    h = pad_t + height * cell + 34
    lo, hi = min(values), max(values)
    span = (hi - lo) or 1.0

    out = [svg_open(w, h, label)]
    for c in range(width):
        out.append(text(pad_l + c * cell + cell / 2, pad_t - 9, chr(ord("A") + c), "tick"))
    for r in range(height):
        out.append(text(pad_l - 10, pad_t + r * cell + cell / 2 + 4, str(r + 1), "tick", "end"))

    for r in range(height):
        for c in range(width):
            v = values[r * width + c]
            b = min(BUCKETS, max(0, int((v - lo) / span * BUCKETS)))
            x, y = pad_l + c * cell, pad_t + r * cell
            # 2px surface gap between fills, per the mark spec.
            out.append(
                f'<rect class="cellmark" x="{x + 1}" y="{y + 1}" width="{cell - 2}" '
                f'height="{cell - 2}" rx="3" fill="var(--ramp-{b})" '
                f'data-tip="{esc(chr(ord("A") + c))}{r + 1}: {esc(fmt_cell(v))}"/>')
            out.append(text(x + cell / 2, y + cell / 2 + 4, fmt_cell(v),
                            "cellval hi" if b >= 7 else "cellval"))
    # Ramp legend, direct-labelled at both ends.
    ly = pad_t + height * cell + 20
    seg = 150 / (BUCKETS + 1)
    out.append(text(pad_l, ly + 4, fmt_cell(lo), "tick", "start"))
    for i in range(BUCKETS + 1):
        out.append(f'<rect x="{pad_l + 44 + i * seg:.1f}" y="{ly - 7}" width="{seg:.1f}" '
                   f'height="10" fill="var(--ramp-{i})"/>')
    out.append(text(pad_l + 44 + 150 + 6, ly + 4, fmt_cell(hi), "tick", "start"))
    out.append(text(pad_l + 44 + 150 + 64, ly + 4, caption_scale, "tick", "start"))
    out.append("</svg>")
    return "".join(out)


def bound_ladder(bounds, policies):
    w, h = 760, 268
    pad_l, pad_r, pad_t = 20, 20, 34
    x0, x1 = 12.0, 50.0
    def sx(v):
        return pad_l + (v - x0) / (x1 - x0) * (w - pad_l - pad_r)

    out = [svg_open(w, h, "Lower-bound ladder against measured policies")]
    base = h - 42
    # Shaded first, so the rungs and points sit on top of it.
    out.append(f'<rect class="gap" x="{sx(bounds["waterfilling"]):.1f}" y="{pad_t - 20}" '
               f'width="{sx(policies[-1]["mean"]) - sx(bounds["waterfilling"]):.1f}" '
               f'height="{base - pad_t + 20:.1f}"/>')
    out.append(axis_line(pad_l, base, w - pad_r, base))
    for v in range(15, 51, 5):
        out.append(axis_line(sx(v), base, sx(v), base + 5, "tick-mark"))
        out.append(text(sx(v), base + 19, str(v), "tick"))
    out.append(text(w / 2, h - 6, "expected shots to clear the board", "axtitle"))

    rows = [
        ("entropy bound", bounds["entropy"], "--muted-ink", "dominated: below the coverage bound"),
        ("coverage bound", float(bounds["coverage"]), "--ink-2", "all 17 ship cells must be shot"),
        ("water-filling bound", bounds["waterfilling"], "--series-1", "binding: proved by transcript counting"),
    ]
    y = pad_t
    for name, v, col, note in rows:
        x = sx(v)
        out.append(f'<line class="rung" x1="{x:.1f}" y1="{y}" x2="{x:.1f}" '
                   f'y2="{base}" stroke="var({col})"/>')
        # To the right of the rung it names, at that rung's own height. The
        # rungs are drawn in ascending order and each label sits on its own row,
        # so a label can only meet a rung standing further right than its own,
        # and there is none. Flip to the left only when the note would leave the
        # plate.
        note_w = 6.1 * len(note)
        if x + 9 + note_w > w - pad_r:
            anchor, lx = "end", x - 9
        else:
            anchor, lx = "start", x + 9
        out.append(text(lx, y + 4, f"{name}  {v:.2f}", "runglbl", anchor))
        out.append(text(lx, y + 20, note, "tick", anchor))
        y += 44

    # Measured policies as points with confidence whiskers.
    for i, p in enumerate(policies):
        if p["mean"] > x1:
            continue
        yy = y + i * 26
        out.append(f'<line class="ci" x1="{sx(p["mean"] - p["ci"]):.1f}" y1="{yy}" '
                   f'x2="{sx(p["mean"] + p["ci"]):.1f}" y2="{yy}"/>')
        out.append(f'<circle class="pt" cx="{sx(p["mean"]):.1f}" cy="{yy}" r="5" '
                   f'fill="var(--series-2)" data-tip="{esc(p["name"])}: '
                   f'{p["mean"]:.3f} +/- {p["ci"]:.3f}"/>')
        out.append(text(sx(p["mean"]) - 11, yy + 4, f'{p["name"]}  {p["mean"]:.2f}',
                        "ptlbl", "end"))

    out.append("</svg>")
    return "".join(out)


def objective_bars(rows):
    w = 760
    row_h, pad_t, pad_l = 62, 30, 108
    h = pad_t + row_h * len(rows) + 66
    hi = max(max(r["maxInfo"], r["density"], r["maxProb"]) for r in rows)
    span = w - pad_l - 128

    out = [svg_open(w, h, "Expected shots by objective against the exact optimum")]
    series = [("optimal", "optimal", "--ink-2"),
              ("max-P(hit)", "maxProb", "--series-1"),
              ("max information gain", "maxInfo", "--series-2")]
    for i, r in enumerate(rows):
        y = pad_t + i * row_h
        out.append(text(pad_l - 12, y + 20, r["instance"], "rowlbl", "end"))
        out.append(text(pad_l - 12, y + 34, f'{r["configurations"]:,} boards', "tick", "end"))
        for j, (_, key, col) in enumerate(series):
            bw = r[key] / hi * span
            by = y + 4 + j * 13
            out.append(f'<rect class="bar" x="{pad_l}" y="{by}" width="{bw:.1f}" height="10" '
                       f'rx="3" fill="var({col})" data-tip="{esc(r["instance"])} '
                       f'{esc(series[j][0])}: {r[key]:.4f} shots"/>')
            out.append(text(pad_l + bw + 6, by + 9, f"{r[key]:.2f}", "barval", "start"))
    out.append(legend(pad_l, h - 38, [(n, c) for n, _, c in series], 168))
    out.append(text(pad_l + span / 2, h - 8, "expected shots (lower is better)", "axtitle"))
    out.append("</svg>")
    return "".join(out)


def survival(policies):
    # The bottom padding carries the shot ticks, the axis title and the key. The
    # key sits under the graph, so the right margin no longer has to reserve room
    # for labels drawn along the curves and the plot gets that width back.
    w, h = 760, 300
    pad_l, pad_r, pad_t, pad_b = 52, 196, 22, 52
    xmax = 100
    def sx(v):
        return pad_l + v / xmax * (w - pad_l - pad_r)
    def sy(p):
        return pad_t + (1 - p) * (h - pad_t - pad_b)

    out = [svg_open(w, h, "Fraction of games still unfinished after n shots")]
    for g in range(0, 11, 2):
        yy = sy(g / 10)
        out.append(axis_line(pad_l, yy, w - pad_r, yy, "grid"))
        out.append(text(pad_l - 8, yy + 4, f"{g * 10}%", "tick", "end"))
    for v in range(0, 101, 20):
        out.append(text(sx(v), h - pad_b + 18, str(v), "tick"))
    out.append(axis_line(pad_l, sy(0), w - pad_r, sy(0)))
    out.append(text((pad_l + w - pad_r) / 2, h - 10, "shots taken", "axtitle"))

    for i, p in enumerate(policies):
        hist = p["histogram"]
        total = sum(hist) or 1
        pts, running = [], total
        for n in range(len(hist)):
            pts.append((sx(n), sy(running / total)))
            running -= hist[n]
        d = " ".join(("M" if k == 0 else "L") + f"{x:.1f},{y:.1f}" for k, (x, y) in enumerate(pts))
        out.append(f'<path class="line" d="{d}" stroke="var({SERIES[i]})"/>')

    # One key, in the right margin, carrying both the colour and the two
    # crossings a survival curve is read for. Off the plot entirely, so nothing
    # sits on a curve and no name is printed twice.
    kx = w - pad_r + 16
    for i, p in enumerate(policies):
        hist = p["histogram"]
        total = sum(hist) or 1
        med = next(n for n in range(len(hist)) if sum(hist[:n + 1]) >= total / 2)
        p95 = next(n for n in range(len(hist)) if sum(hist[:n + 1]) >= 0.95 * total)
        ky = pad_t + 16 + i * 40
        out.append(f'<rect x="{kx:.1f}" y="{ky - 9:.1f}" width="10" height="10" rx="2" '
                   f'fill="var({SERIES[i]})"/>')
        out.append(text(kx + 16, ky, p["name"], "serieslbl", "start",
                        f'fill="var({SERIES[i]})"'))
        out.append(text(kx + 16, ky + 15, f"median {med}, 95% by {p95}", "tick", "start"))
    # 17 is a hard floor for every policy.
    out.append(f'<line class="floor" x1="{sx(17):.1f}" y1="{sy(0)}" x2="{sx(17):.1f}" y2="{sy(1)}"/>')
    out.append(text(sx(17) + 5, sy(0.06), "17, the coverage bound", "tick", "start"))
    out.append("</svg>")
    return "".join(out)


def collapse(games, omega0):
    # The bottom padding carries three stacked rows below the plot: the shot
    # ticks, the axis title, and the key. The key sits under the graph rather
    # than inside it, where it used to overlap the opening of every curve.
    w, h = 760, 340
    pad_l, pad_r, pad_t, pad_b = 56, 24, 22, 86
    xmax = max(len(g["omega"]) for g in games)
    top = math.log10(omega0)
    def sx(v):
        return pad_l + v / xmax * (w - pad_l - pad_r)
    def sy(v):
        return pad_t + (1 - (math.log10(max(v, 1)) / top)) * (h - pad_t - pad_b)

    out = [svg_open(w, h, "Hypothesis count collapsing over one game")]
    for e in range(0, 11, 2):
        yy = sy(10 ** e)
        out.append(axis_line(pad_l, yy, w - pad_r, yy, "grid"))
        out.append(text(pad_l - 8, yy + 4, f"10^{e}" if e else "1", "tick", "end"))
    for v in range(0, xmax + 1, 10):
        out.append(text(sx(v), h - pad_b + 18, str(v), "tick"))
    out.append(axis_line(pad_l, sy(1), w - pad_r, sy(1)))
    out.append(text((pad_l + w - pad_r) / 2, h - pad_b + 40, "shots taken", "axtitle"))

    # Centred under the axis title. legend() lays entries out from its x at a
    # fixed gap, so the span is the gaps plus room for the last label.
    gap = 96
    span = (len(games) - 1) * gap + 62
    out.append(legend((w - span) / 2, h - 14,
                      [(f'game {g["game"]}', SERIES[i]) for i, g in enumerate(games)], gap))

    for i, g in enumerate(games):
        pts = [(sx(n), sy(v)) for n, v in enumerate(g["omega"])]
        d = " ".join(("M" if k == 0 else "L") + f"{x:.1f},{y:.1f}" for k, (x, y) in enumerate(pts))
        out.append(f'<path class="line" d="{d}" stroke="var({SERIES[i]})"/>')
        # Mark the sinks: each one is a step down worth seeing.
        for n, o in enumerate(g["outcomes"]):
            if o == 2:
                out.append(f'<circle class="sink" cx="{sx(n + 1):.1f}" cy="{sy(g["omega"][n + 1]):.1f}" '
                           f'r="3.5" fill="var({SERIES[i]})" data-tip="ship sunk at shot {n + 1}"/>')
    # Stacked in the top right, which every curve has left empty by the time it
    # gets there: the counts fall monotonically, so the right of the plot is its
    # floor. Printing these along the lines put three labels into the busiest
    # part of the figure.
    for i, g in enumerate(games):
        out.append(text(w - pad_r - 4, pad_t + 12 + i * 16,
                        f'game {g["game"]}, {g["shots"]} shots', "serieslbl", "end",
                        f'fill="var({SERIES[i]})"'))
    out.append("</svg>")
    return "".join(out)


def layer_profile(sizes, peak):
    w, h = 760, 228
    pad_l, pad_r, pad_t, pad_b = 56, 20, 20, 56
    n = len(sizes)
    def sx(i):
        return pad_l + i / (n - 1) * (w - pad_l - pad_r)
    def sy(v):
        return pad_t + (1 - v / peak) * (h - pad_t - pad_b)

    out = [svg_open(w, h, "Live states entering each cell layer of the lattice")]
    for f in (0, 0.5, 1.0):
        yy = sy(peak * f)
        out.append(axis_line(pad_l, yy, w - pad_r, yy, "grid"))
        out.append(text(pad_l - 8, yy + 4, fmt(peak * f), "tick", "end"))
    d = " ".join(("M" if k == 0 else "L") + f"{sx(k):.1f},{sy(v):.1f}" for k, v in enumerate(sizes))
    out.append(f'<path class="area" d="{d} L{sx(n - 1):.1f},{sy(0):.1f} L{sx(0):.1f},{sy(0):.1f} Z"/>')
    out.append(f'<path class="line" d="{d}" stroke="var(--series-1)"/>')
    # Every column boundary, drawn. The profile drops at each one because a
    # vertical ship has to fit inside its column, so the run counter is
    # necessarily zero on the first cell of the next: that layer is a strict
    # subset of its neighbours. Without the boundaries the drops read as noise.
    for col in range(0, 11):
        x = sx(min(col * 10, n - 1))
        if 0 < col < 10:
            out.append(axis_line(x, sy(0), x, pad_t, "grid"))
        out.append(axis_line(x, sy(0), x, sy(0) + 4, "tick-mark"))
        if col < 10:
            out.append(text(x, h - pad_b + 18, str(col), "tick"))
    out.append(axis_line(pad_l, sy(0), w - pad_r, sy(0)))
    out.append(text((pad_l + w - pad_r) / 2, h - 6, "cell layer, swept column by column", "axtitle"))
    out.append("</svg>")
    return "".join(out)


def orbit_map(counts, total, width, height, cell=46):
    """The D4 orbits of the board, coloured by orbit and carrying their exact
    integer counts. Reflections and the diagonal generate 15 classes on a 10x10,
    so a per-cell quantity needs 15 evaluations and not 100."""
    pad_l, pad_t = 34, 26
    w = pad_l + width * cell + 12
    h = pad_t + height * cell + 40

    # Orbit representative for a cell, under the dihedral group.
    def rep(r, c):
        a, b = min(r, height - 1 - r), min(c, width - 1 - c)
        return (min(a, b), max(a, b))

    reps = sorted({rep(r, c) for r in range(height) for c in range(width)})
    index = {k: i for i, k in enumerate(reps)}

    out = [svg_open(w, h, "The 15 dihedral orbits of the board")]
    for c in range(width):
        out.append(text(pad_l + c * cell + cell / 2, pad_t - 9, chr(ord("A") + c), "tick"))
    for r in range(height):
        out.append(text(pad_l - 10, pad_t + r * cell + cell / 2 + 4, str(r + 1), "tick", "end"))

    for r in range(height):
        for c in range(width):
            k = rep(r, c)
            i = index[k]
            b = int(i / (len(reps) - 1) * BUCKETS)
            x, y = pad_l + c * cell, pad_t + r * cell
            n = counts[k[0] * width + k[1]]
            out.append(
                f'<rect class="cellmark" x="{x + 1}" y="{y + 1}" width="{cell - 2}" '
                f'height="{cell - 2}" rx="3" fill="var(--ramp-{b})" '
                f'data-tip="orbit {i + 1} of {len(reps)}: {n:,} configurations, '
                f'{n / total:.6f}"/>')
            out.append(text(x + cell / 2, y + cell / 2 + 4, str(i + 1),
                            "cellval hi" if b >= 7 else "cellval"))
    out.append(text(pad_l, pad_t + height * cell + 22,
                    f"{len(reps)} orbits, so a per-cell quantity costs {len(reps)} "
                    f"evaluations and not {width * height}", "tick", "start"))
    out.append("</svg>")
    return "".join(out)


def blocking_boards(witnesses, width, height, cell=26):
    """Each witness set drawn on its own board: shoot these cells and no
    placement of that length can survive untouched."""
    per = pad_l = 34
    bw = width * cell + 16
    w = len(witnesses) * bw
    h = 34 + height * cell + 30

    out = [svg_open(w, h, "Blocking sets: the fewest shots that meet every placement")]
    for i, wit in enumerate(witnesses):
        ox = i * bw + 8
        marked = set(wit["cells"])
        out.append(text(ox + width * cell / 2, 16,
                        f'length {wit["length"]}: beta = {wit["beta"]}', "rowlbl"))
        for r in range(height):
            for c in range(width):
                x, y = ox + c * cell, 28 + r * cell
                on = (r * width + c) in marked
                out.append(
                    f'<rect class="cellmark" x="{x + 1}" y="{y + 1}" width="{cell - 2}" '
                    f'height="{cell - 2}" rx="2" '
                    f'fill="{"var(--series-1)" if on else "var(--ramp-0)"}"/>')
        # The label has to describe the drawing. Titling a 34-cell greedy cover
        # "beta = 33" leaves a reader who counts the marks with the wrong number.
        note = ("minimum" if wit.get("optimal")
                else f'a greedy cover of {len(wit["cells"])}')
        out.append(text(ox + width * cell / 2, 28 + height * cell + 18, note, "tick"))
    out.append("</svg>")
    return "".join(out)


def opening_book(steps, width, height, cell=46):
    """The greedy line down the all-miss branch, drawn as a ranking.

    Ranked cells carry their position in the order and are shaded by it, earliest
    darkest, using the same blue ramp and the same ink rule as every other board
    on the page. Cells the line never reaches keep the base tint."""
    pad_l, pad_t = 34, 26
    w = pad_l + width * cell + 12
    h = pad_t + height * cell + 34

    rank = {st["cell"]: i + 1 for i, st in enumerate(steps)}
    last = max(1, len(steps) - 1)

    out = [svg_open(w, h, "Order to attack in while every answer is a miss")]
    for c in range(width):
        out.append(text(pad_l + c * cell + cell / 2, pad_t - 9, chr(ord("A") + c), "tick"))
    for r in range(height):
        out.append(text(pad_l - 9, pad_t + r * cell + cell / 2 + 4, str(r + 1), "tick", "end"))

    for r in range(height):
        for c in range(width):
            i = r * width + c
            x, y = pad_l + c * cell, pad_t + r * cell
            if i in rank:
                k = rank[i]
                b = BUCKETS - int((k - 1) / last * (BUCKETS - 1))
                tip = f"{chr(ord('A') + c)}{r + 1}: shot {k} of the line"
            else:
                b = 0
                tip = f"{chr(ord('A') + c)}{r + 1}: not reached before the line ends"
            out.append(f'<rect class="cellmark" x="{x + 1}" y="{y + 1}" '
                       f'width="{cell - 2}" height="{cell - 2}" rx="3" '
                       f'fill="var(--ramp-{b})" data-tip="{tip}"/>')
            if i in rank:
                out.append(text(x + cell / 2, y + cell / 2 + 4, str(rank[i]),
                                "cellval hi" if b >= 7 else "cellval"))
    out.append(text(pad_l + width * cell / 2, h - 10,
                    "rank in the all-miss line", "axtitle"))
    out.append("</svg>")
    return "".join(out)


def order_dependence(payload, cell=40):
    """The same shots in two orders, with the posterior each one implies."""
    width, height = payload["width"], payload["height"]
    orders = payload["orders"]
    bw = width * cell + 30
    w = len(orders) * bw
    h = 34 + height * cell + 34

    out = [svg_open(w, h, "The same shots in two orders give different posteriors")]
    for i, o in enumerate(orders):
        ox = i * bw + 12
        turn = {c: t + 1 for t, c in enumerate(o["cells"])}
        kind = {c: k for c, k in zip(o["cells"], o["kinds"])}
        out.append(text(ox + width * cell / 2, 16,
                        f'order {chr(65 + i)}   {o["omega"]} boards survive', "rowlbl"))
        for r in range(height):
            for c in range(width):
                idx = r * width + c
                x, y = ox + c * cell, 28 + r * cell
                k = kind.get(idx)
                fill = "var(--ramp-0)"
                if k == 0: fill = "var(--grid)"
                elif k == 1: fill = "var(--series-2)"
                elif k == 2: fill = "var(--series-3)"
                out.append(
                    f'<rect class="cellmark" x="{x + 1}" y="{y + 1}" width="{cell - 2}" '
                    f'height="{cell - 2}" rx="3" fill="{fill}"/>')
                if idx in turn:
                    out.append(text(x + cell / 2, y + cell / 2 + 4, str(turn[idx]),
                                    "cellval hi" if k else "cellval"))
    # A compact legend, since the full sentence does not fit the plate.
    out.append(legend(12, h - 10, [("miss", "--grid"), ("hit", "--series-2"),
                                   ("sank it", "--series-3")], 92))
    out.append(text(w - 12, h - 9, "number is the shot order", "tick", "end"))
    out.append("</svg>")
    return "".join(out)


def scaling(rows):
    # Both scales are inset by a marker's width, so the extreme points sit inside
    # the frame instead of straddling the axes they are measured against.
    w, h = 400, 258
    pad_l, pad_r, pad_t, pad_b = 62, 20, 20, 48
    inset = 14
    lo = math.log10(min(r["omega"] for r in rows))
    hi = math.log10(max(r["omega"] for r in rows))
    ns = [r["n"] for r in rows]
    x_lo, x_hi = pad_l + inset, w - pad_r - inset
    y_lo, y_hi = h - pad_b - inset, pad_t + inset

    def sx(n):
        return x_lo + (n - min(ns)) / (max(ns) - min(ns)) * (x_hi - x_lo)

    def sy(v):
        return y_lo + (math.log10(v) - lo) / (hi - lo) * (y_hi - y_lo)

    out = [svg_open(w, h, "Configuration count against board size")]
    for e in range(int(lo), int(hi) + 2):
        if not (lo <= e <= hi):
            continue
        yy = sy(10 ** e)
        out.append(axis_line(pad_l, yy, w - pad_r, yy, "grid"))
        out.append(text(pad_l - 8, yy + 4, f"10^{e}", "tick", "end"))
    d = " ".join(("M" if k == 0 else "L") + f"{sx(r['n']):.1f},{sy(r['omega']):.1f}"
                 for k, r in enumerate(rows))
    out.append(f'<path class="line" d="{d}" stroke="var(--series-1)"/>')
    for r in rows:
        out.append(f'<circle class="pt" cx="{sx(r["n"]):.1f}" cy="{sy(r["omega"]):.1f}" r="4.5" '
                   f'fill="var(--series-1)" data-tip="{r["n"]}x{r["n"]}: {r["omega"]:,}"/>')
        out.append(text(sx(r["n"]), h - pad_b + 18, f'{r["n"]}', "tick"))
    # Both axes, drawn, so the reader can see what the points are measured from.
    out.append(axis_line(pad_l, h - pad_b, w - pad_r, h - pad_b))
    out.append(axis_line(pad_l, pad_t, pad_l, h - pad_b))
    out.append(text((pad_l + w - pad_r) / 2, h - 8, "board side N, for an NxN board", "axtitle"))
    out.append(text(16, (pad_t + h - pad_b) / 2, "legal configurations", "axtitle", "middle",
                    f'transform="rotate(-90 16 {(pad_t + h - pad_b) / 2:.1f})"'))
    out.append("</svg>")
    return "".join(out)
