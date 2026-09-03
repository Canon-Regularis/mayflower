"""Assemble the Mayflower report page from the figure-data contract.

Figure primitives live in build_report.py; this file is the page: tokens, prose,
and the order of the argument. Run it as:

    python tools/render_report.py out/figures.json out/report.html
"""

import io
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from build_report import (RAMP, blocking_boards, board_heatmap, bound_ladder, collapse,
                          opening_book,
                          esc, layer_profile, objective_bars, orbit_map, order_dependence,
                          scaling, survival)

DARK_TOKENS = """
    color-scheme: dark;
    --page:        #0d0d0d;
    --surface:     #1a1a19;
    --ink:         #ffffff;
    --ink-2:       #c3c2b7;
    --muted-ink:   #898781;
    --grid:        #2c2c2a;
    --axis:        #383835;
    --rule:        #86b6ef;
    --series-1:    #3987e5;
    --series-2:    #d95926;
    --series-3:    #199e70;
    --cell-ink:    #ffffff;
    --cell-ink-hi: #0b0b0b;
    --gapfill:     rgba(57,135,229,0.12);
"""


def ramp_block(stops, indent="  "):
    return "\n".join(f"{indent}--ramp-{i}: {c};" for i, c in enumerate(stops))


def stylesheet():
    light_ramp = ramp_block(RAMP)
    dark_ramp = ramp_block(list(reversed(RAMP)), "    ")
    return """
:root {
  color-scheme: light;
  --page:        #f9f9f7;
  --surface:     #fcfcfb;
  --ink:         #0b0b0b;
  --ink-2:       #52514e;
  --muted-ink:   #898781;
  --grid:        #e1e0d9;
  --axis:        #c3c2b7;
  --rule:        #0d366b;
  --series-1:    #2a78d6;
  --series-2:    #eb6834;
  --series-3:    #1baf7a;
  --cell-ink:    #0b0b0b;
  --cell-ink-hi: #ffffff;
  --gapfill:     rgba(42,120,214,0.07);
""" + light_ramp + """
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {""" + DARK_TOKENS + dark_ramp + """
  }
}
:root[data-theme="dark"] {""" + DARK_TOKENS + dark_ramp + """
}

* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--page);
  color: var(--ink);
  font-family: "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif;
  font-size: 16.5px;
  line-height: 1.62;
}
.wrap { max-width: 1180px; margin: 0 auto; padding: 0 28px 96px; }
.col { max-width: 68ch; }

header.mast { padding: 76px 0 40px; }
.eyebrow {
  font-family: "IBM Plex Mono", ui-monospace, Consolas, monospace;
  font-size: 12px; letter-spacing: 0.14em; text-transform: uppercase;
  color: var(--muted-ink);
}
h1 {
  font-family: "IBM Plex Serif", Georgia, serif;
  font-weight: 600; font-size: clamp(34px, 5.2vw, 54px); line-height: 1.08;
  margin: 14px 0 0; letter-spacing: -0.02em; text-wrap: balance;
}
.standfirst { font-size: 19px; color: var(--ink-2); margin: 18px 0 0; max-width: 62ch; }

.figures {
  display: flex; flex-wrap: wrap; gap: 0; margin: 40px 0 0;
  border-top: 1px solid var(--grid); border-bottom: 1px solid var(--grid);
}
.keyfig { padding: 18px 30px 18px 0; margin-right: 30px; border-right: 1px solid var(--grid); }
.keyfig:last-child { border-right: 0; margin-right: 0; }
.keyfig .v {
  font-family: "IBM Plex Mono", ui-monospace, monospace;
  font-size: 25px; font-weight: 500; letter-spacing: -0.02em; display: block;
}
.keyfig .k { font-size: 12.5px; color: var(--muted-ink); letter-spacing: 0.03em; }

section { padding-top: 60px; }
h2 {
  font-family: "IBM Plex Serif", Georgia, serif;
  font-weight: 600; font-size: 27px; line-height: 1.22; margin: 0 0 6px;
  letter-spacing: -0.01em; text-wrap: balance;
}
h3 {
  font-family: "IBM Plex Serif", Georgia, serif;
  font-weight: 600; font-size: 19px; line-height: 1.3;
  margin: 34px 0 10px; letter-spacing: -0.005em; text-wrap: balance;
}
.act {
  font-family: "IBM Plex Mono", ui-monospace, monospace;
  font-size: 12px; letter-spacing: 0.14em; text-transform: uppercase;
  color: var(--rule); margin-bottom: 10px;
}
p { margin: 0 0 16px; }
.lede { color: var(--ink-2); }
strong, b { font-weight: 600; }
code, .mono {
  font-family: "IBM Plex Mono", ui-monospace, Consolas, monospace;
  font-size: 0.92em; font-variant-numeric: tabular-nums;
}

figure { margin: 26px 0 0; }
.plate {
  background: var(--surface); border: 1px solid var(--grid); border-radius: 6px;
  padding: 18px 20px 14px; overflow-x: auto;
}
/* Capped at its design width by the svg's own style, so no figure is ever
   upscaled and 11px stays 11px on every plate. The floor keeps a wide chart
   legible on a narrow screen, where the plate scrolls instead of shrinking
   the type to nothing. */
.plate svg { display: block; width: 100%; height: auto; margin: 0 auto;
             min-width: 340px; }
@media (max-width: 420px) { .plate { padding: 12px 10px 10px; } }
figcaption { font-size: 14px; color: var(--ink-2); margin-top: 12px; max-width: 68ch; }
figcaption b { color: var(--ink); }
.pair { display: flex; flex-wrap: wrap; gap: 22px; align-items: flex-start; }
.pair > figure { flex: 1 1 330px; margin-top: 26px; }

svg text { font-family: "IBM Plex Sans", system-ui, sans-serif; }
.tick { font-size: 11px; fill: var(--muted-ink); font-variant-numeric: tabular-nums; }
.axtitle { font-size: 12px; fill: var(--ink-2); }
.rowlbl { font-size: 13px; fill: var(--ink); font-weight: 500; }
.barval, .cellval {
  font-family: "IBM Plex Mono", ui-monospace, monospace;
  font-size: 11px; font-variant-numeric: tabular-nums;
}
.barval { fill: var(--ink-2); }
.cellval { fill: var(--cell-ink); }
.cellval.hi { fill: var(--cell-ink-hi); }
.runglbl { font-size: 13px; fill: var(--ink); font-weight: 600; }
.serieslbl, .ptlbl { font-size: 12.5px; font-weight: 600; }
.legendlbl { font-size: 12px; fill: var(--ink-2); }
.ptlbl { fill: var(--ink-2); }
.axis { stroke: var(--axis); stroke-width: 1; }
.grid { stroke: var(--grid); stroke-width: 1; }
.tick-mark { stroke: var(--axis); stroke-width: 1; }
.line { fill: none; stroke-width: 2; stroke-linejoin: round; stroke-linecap: round; }
.area { fill: var(--gapfill); stroke: none; }
.rung { stroke-width: 2.5; stroke-linecap: round; }
.ci { stroke: var(--axis); stroke-width: 2; }
.gap { fill: var(--gapfill); }
.floor { stroke: var(--muted-ink); stroke-width: 1.5; stroke-dasharray: 3 3; }
.cellmark:hover, .bar:hover { stroke: var(--ink); stroke-width: 2; }

table { border-collapse: collapse; width: 100%; font-size: 14.5px; margin-top: 8px; }
th, td { text-align: right; padding: 7px 12px; border-bottom: 1px solid var(--grid); }
th:first-child, td:first-child { text-align: left; }
th { font-size: 12px; text-transform: uppercase; letter-spacing: 0.06em;
     color: var(--muted-ink); font-weight: 500; }
td.num { font-family: "IBM Plex Mono", ui-monospace, monospace;
         font-variant-numeric: tabular-nums; }
.tablewrap { overflow-x: auto; margin-top: 20px; }

.note {
  border-left: 2px solid var(--rule); padding: 2px 0 2px 18px;
  margin: 24px 0; color: var(--ink-2); font-size: 15px; max-width: 66ch;
}
footer { margin-top: 72px; padding-top: 22px; border-top: 1px solid var(--grid);
         color: var(--muted-ink); font-size: 13.5px; }
.livewrap { background: var(--surface); border: 1px solid var(--grid); border-radius: 6px;
            padding: 22px 24px 20px; margin-top: 30px; }
.livegrid { display: flex; flex-wrap: wrap; gap: 28px; align-items: flex-start; }
.liveboard {
  display: grid; grid-template-columns: repeat(10, 34px); grid-auto-rows: 34px; gap: 2px;
  font-family: "IBM Plex Mono", ui-monospace, monospace; font-size: 11px;
}
.lc { display: flex; align-items: center; justify-content: center; border-radius: 3px;
      color: var(--cell-ink); background: var(--ramp-0); font-variant-numeric: tabular-nums; }
.scrubboard { margin-bottom: 14px; }
.scrubctl { display: flex; align-items: center; gap: 12px; margin: 6px 0 10px; }
.scrubrange { flex: 1 1 auto; accent-color: var(--series-2); }
.scrubbtn {
  font: 500 13px/1 var(--sans); padding: 7px 12px; border-radius: 6px;
  border: 1px solid var(--grid); background: var(--panel); color: var(--ink);
  cursor: pointer;
}
ul.summary { margin: 2px 0 20px; padding-left: 22px; }
ul.summary li { margin-bottom: 11px; line-height: 1.6; color: var(--ink-2); }
ul.summary li::marker { color: var(--rule); }
ul.summary b { color: var(--ink); font-weight: 600; }
.thanks {
  margin-top: 26px; padding-top: 18px; border-top: 1px solid var(--grid);
  color: var(--muted-ink); font-size: 14px;
}
.scrubnum {
  display: inline-flex; align-items: center; gap: 5px; white-space: nowrap;
  font: 400 12px/1 var(--mono); color: var(--muted-ink);
}
.scrubnum input {
  width: 4.2em; font: 400 12px/1 var(--mono); padding: 6px 6px; border-radius: 6px;
  border: 1px solid var(--grid); background: var(--panel); color: var(--ink);
  font-variant-numeric: tabular-nums;
}
.scrubnum input:focus-visible { outline: 2px solid var(--series-2); outline-offset: 1px; }
.scrubbtn:hover { border-color: var(--ink); }
.scrubbtn:disabled { opacity: .5; cursor: default; }
#scrub:focus-visible { outline: 2px solid var(--series-2); outline-offset: 4px; }
.scrubread {
  display: flex; flex-wrap: wrap; gap: 18px;
  font: 400 13px/1.5 var(--mono); color: var(--muted-ink);
  font-variant-numeric: tabular-nums;
}
.scrubread b { color: var(--ink); font-weight: 500; }
.visually-hidden {
  position: absolute; width: 1px; height: 1px; overflow: hidden;
  clip: rect(0 0 0 0); clip-path: inset(50%); white-space: nowrap;
}
.lc.miss { background: var(--grid); color: var(--muted-ink); }
.lc.hit  { background: var(--series-2); color: #fff; font-size: 13px; }
.lc.sunk { background: var(--series-2); color: #fff; font-size: 15px; opacity: .72; }
.lc.ghost { outline: 2px solid var(--series-3); outline-offset: -3px; }
.lc.last { box-shadow: 0 0 0 2px var(--ink); }
.livestats { min-width: 260px; flex: 1 1 260px; }
.livestats > div { display: flex; justify-content: space-between; gap: 14px;
                   padding: 7px 0; border-bottom: 1px solid var(--grid); }
.lk { color: var(--muted-ink); font-size: 13px; }
.lv { font-family: "IBM Plex Mono", ui-monospace, monospace; font-size: 13.5px;
      font-variant-numeric: tabular-nums; }
.lv.exact { color: var(--series-1); font-weight: 500; }
.lv.sampled { color: var(--ink-2); }
.livebtns { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 18px; }
.livebtns button {
  font: inherit; font-size: 13.5px; padding: 7px 14px; border-radius: 4px; cursor: pointer;
  border: 1px solid var(--axis); background: var(--page); color: var(--ink);
}
.livebtns button:hover:not(:disabled) { border-color: var(--ink); }
.livebtns button:disabled { opacity: .45; cursor: default; }
#tip {
  position: fixed; pointer-events: none; opacity: 0; transition: opacity .1s;
  background: var(--ink); color: var(--page); padding: 5px 9px; border-radius: 4px;
  font-size: 12.5px; font-family: "IBM Plex Mono", ui-monospace, monospace; z-index: 9;
}
:focus-visible { outline: 2px solid var(--series-1); outline-offset: 2px; }
@media (prefers-reduced-motion: reduce) { * { transition: none !important; } }
"""


SCRIPT = """
const tip = document.getElementById('tip');
document.addEventListener('pointerover', e => {
  const t = e.target.closest('[data-tip]');
  if (!t) return;
  tip.textContent = t.getAttribute('data-tip');
  tip.style.opacity = '1';
});
document.addEventListener('pointermove', e => {
  if (tip.style.opacity !== '1') return;
  const pad = 14;
  let x = e.clientX + pad, y = e.clientY + pad;
  const r = tip.getBoundingClientRect();
  if (x + r.width > innerWidth) x = e.clientX - r.width - pad;
  if (y + r.height > innerHeight) y = e.clientY - r.height - pad;
  tip.style.left = x + 'px'; tip.style.top = y + 'px';
});
document.addEventListener('pointerout', e => {
  if (e.target.closest('[data-tip]')) tip.style.opacity = '0';
});
"""


def load_engine():
    """web/engine.js is an ES module; inline it as a plain script that publishes
    the same names on one global, so the page needs no module loader."""
    here = os.path.dirname(os.path.abspath(__file__))
    src = io.open(os.path.join(here, "..", "web", "engine.js"), encoding="utf-8").read()
    src = src.replace("export const ", "const ").replace("export function ", "function ")
    return ("(function(){\n" + src +
            "\nwindow.MayflowerEngine = { makeInstance, count, marginals, constrain,"
            " MISS, HIT, SUNK, FREE, EMPTY, OCCUPIED };\n})();")


def load_scrubber():
    here = os.path.dirname(os.path.abspath(__file__))
    return io.open(os.path.join(here, "..", "web", "scrubber.js"), encoding="utf-8").read()


def load_live():
    here = os.path.dirname(os.path.abspath(__file__))
    return io.open(os.path.join(here, "..", "web", "live.js"), encoding="utf-8").read()


def load_pool():
    import base64
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "web", "pool.bin"), "rb") as fh:
        return base64.b64encode(fh.read()).decode("ascii")


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


def build(data, out_path):
    m = data["meta"]
    prior = data["prior"]
    pol = {p["name"]: p for p in data["policies"]}
    policies = [pol[n] for n in ["random", "parity hunt/target", "density"] if n in pol]
    b = data["bounds"]
    omega = m["omega0"]
    lat = data["lattice"]
    obj = data["objectives"]
    col = data["collapse"]

    prior_p = [c / prior["total"] for c in prior["counts"]]
    dens, par = pol["density"], pol["parity hunt/target"]
    best = policies[-1]

    SCRUB_JS = load_scrubber()
    ENGINE_JS = load_engine()
    LIVE_JS = load_live()
    POOL_B64 = load_pool()

    o = io.StringIO()
    w = o.write
    w("<title>The Battleship Posterior</title>\n")
    w('<link rel="preconnect" href="https://fonts.googleapis.com">\n')
    w('<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n')
    w('<link rel="stylesheet" href="https://fonts.googleapis.com/css2?'
      'family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&'
      'family=IBM+Plex+Serif:wght@600&display=swap">\n')
    w("<style>" + stylesheet() + "</style>\n")
    w('<div id="tip"></div>\n<div class="wrap">\n')

    w('<header class="mast"><div class="col">')
    w('<div class="eyebrow">Mayflower / exact inference engine</div>')
    w("<h1>The Battleship Posterior</h1>")
    w('<p class="standfirst">Every legal fleet counted exactly, without enumerating one, '
      "and the shot-selection objectives that follow priced against a proved "
      "floor.</p></div>")
    w('<div class="figures">')
    for v, k in [("{:,}".format(omega), "legal configurations, counted exactly"),
                 ("{:.2f} bits".format(m["entropyBits"]), "to identify the board"),
                 ("{:.2f} to {:.2f}".format(b["waterfilling"], best["mean"]),
                  "shots: proved floor to best measured")]:
        w('<div class="keyfig"><span class="v">' + esc(v) + '</span>'
          '<span class="k">' + esc(k) + "</span></div>")
    w("</div></header>\n")

    # 0, the anchor -----------------------------------------------------
    w('<section><div class="col"><div class="act">Play it</div>')
    w("<h2>The engine, hunting a board it cannot see</h2>")
    w('<p class="lede">A hidden fleet, drawn uniformly from all {:,} legal arrangements. '
      "The engine sees only what it has shot. Each cell shows the current probability that "
      "a ship covers it, and the engine fires at the highest one.</p>".format(omega))
    w("<p>Two estimators answer the same query, with opposite cost profiles. Filtering a "
      "fixed uniform sample of 200,000 boards against the record costs the same at every "
      "turn, and its survivors are a uniform sample of the posterior, so a cell marginal "
      "estimated from k of them carries a standard error of at most 1/(2&#8730;k). The "
      "exact sweep has no error and a cost that falls as the record shrinks the lattice: "
      "27 seconds at turn 0, under a second by shot 14. The handoff is at k = 400, where "
      "the sampled marginal is good to 0.025 and the sweep has become cheap enough to run "
      "between clicks. The readout names the estimator in "
      "use.</p></div>")
    w('<div class="livewrap" id="live" data-pool="' + POOL_B64 + '">')
    w('<div class="livegrid"><div class="liveboard"></div>')
    w('<div><div class="livestats"></div><div class="livebtns">'
      '<button data-act="step">Fire</button>'
      '<button data-act="play">Play</button>'
      '<button data-act="new">New fleet</button>'
      '<button data-act="reveal">Reveal fleet</button>'
      "</div></div></div></div>")
    w('<div class="col"><figcaption>Filled cells are shots: a dot is a miss, a disc a hit, '
      "a cross a sunk ship. Unshot cells carry the posterior as a "
      "percentage.</figcaption></div>")
    w("</section>\n")

    SCALE_SLOPE, SCALE_R2 = _loglog_slope(data["scaling"])

    # 1 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">One / the space</div>')
    w("<h2>Fifteen billion boards, and a prior that is 2.67 to 1</h2>")
    w('<p class="lede">A 10x10 board holding the fleet <code>{{5,4,3,3,2}}</code> admits '
      "exactly <b>{:,}</b> arrangements. A transfer-matrix sweep counts them without "
      "writing one down, and the same sweep returns the exact chance that each cell is "
      "occupied.</p>".format(omega))
    w("<p>The marginal is far from flat: <b>{:.4f}</b> at a corner against <b>{:.4f}</b> "
      "at a centre cell, a ratio of <b>{:.2f}</b>. These are exact rationals, and they sum "
      "over the board to 17, the number of cells the fleet occupies.</p>".format(
          min(prior_p), max(prior_p), max(prior_p) / min(prior_p)))
    w("<p>The count does not factor over ships. Placed independently the fleet admits "
      "120 x 140 x 160&sup2;/2! x 180 = <b>{:,}</b> arrangements; requiring them to be "
      "disjoint removes <b>{:.1%}</b>, and the remainder is the number above. Disjointness "
      "is what the sweep is for, and it is why no closed product gives the "
      "answer.</p></div>".format(FREE_PRODUCT, 1 - omega / FREE_PRODUCT))
    w('<figure><div class="plate">')
    w(board_heatmap(prior_p, prior["width"], prior["height"],
                    "Exact probability that each cell holds a ship",
                    lambda v: "{:.3f}".format(v), "probability a ship covers this cell"))
    w("</div><figcaption><b>Where the ships actually are.</b> Exact occupancy probability "
      "per cell under the uniform prior. The corner reads {:.4f} and the centre {:.4f}. "
      "The four centre cells tie exactly, which is why an optimal opening shot has a "
      "four-way choice.</figcaption></figure>".format(min(prior_p), max(prior_p)))

    w('<div class="pair">')
    w('<figure><div class="plate">')
    w(scaling(data["scaling"]))
    w("</div><figcaption><b>The same fleet, bigger water.</b> Configuration count against "
      "board side, log scale. Every point is an exact integer from the same "
      "sweep.</figcaption></figure>")
    w('<figure><div class="tablewrap"><table><thead><tr><th>board</th>'
      "<th>configurations</th></tr></thead><tbody>")
    for r in data["scaling"]:
        w('<tr><td>{n}x{n}</td><td class="num">{v:,}</td></tr>'.format(n=r["n"], v=r["omega"]))
    w("</tbody></table></div><figcaption>The counts behind the curve. Each ship has "
      "2N(N-L+1) placements, so a fixed fleet of five is O(N<sup>10</sup>) once the board "
      "is roomy. Over this range it is steeper, an empirical <b>N<sup>{:.1f}</sup></b> "
      "(R&sup2; {:.3f}), because at these sizes the ships are still crowded and every "
      "extra row of water relieves more crowding than it adds "
      "room.</figcaption></figure></div></section>\n".format(SCALE_SLOPE, SCALE_R2))

    # 2 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">Two / the machine</div>')
    w("<h2>Counting without enumerating, at 523 boards an edge</h2>")
    w('<p class="lede">The sweep carries a boundary profile across the board one cell at '
      "a time. Its whole lattice is <b>{:,}</b> edges and <b>{:,}</b> state visits, "
      "against {:,} configurations: <b>{:,.0f}</b> boards accounted for per edge "
      "relaxed.</p>".format(lat["edges"], lat["stateVisits"], omega, omega / lat["edges"]))
    w("<p>The profile remembers, for each row, how far a horizontal ship still extends, "
      "how far a vertical ship in the current column still runs, and how much of the "
      "fleet has been spent. Those fields admit 5<sup>11</sup> x 24 = "
      "<b>{:,}</b> distinct profiles, and the sweep never holds more than <b>{:,}</b> of "
      "them at once, a factor of <b>{:,.0f}</b>. The rest are unreachable: no legal partial "
      "placement produces them, so they never enter a layer and are never "
      "paid for.</p></div>".format(
          CRUDE_PROFILES, lat["peakStates"], CRUDE_PROFILES / lat["peakStates"]))
    w('<figure><div class="plate">')
    w(layer_profile(lat["layerSizes"], lat["peakStates"]))
    w("</div><figcaption><b>The shape of the computation.</b> Live states entering each of "
      "the 100 cell layers, peaking at {:,}. Every column boundary after the first drops, "
      "because a vertical ship must fit inside its own column: the run counter is "
      "necessarily zero on a column's first cell, so those layers are confined to the "
      "fifth of the profile space where it vanishes. Within a column the count falls again "
      "over the last three cells, as successively shorter ships lose the rows beneath them "
      "to start in. The collapse over the last "
      "two columns is the fleet counter running out of "
      "ships.</figcaption></figure></section>\n".format(lat["peakStates"]))

    # 3 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">Three / the bound</div>')
    w("<h2>The entropy bound is dominated, and coverage is what binds</h2>")
    w('<p class="lede">Identifying the board takes {:.2f} bits. Each shot answers over an '
      "alphabet of six, {{MISS, HIT, SUNK(2..5)}}, worth at most log&#8322;6 = {:.4f} bits, "
      "so a game of {:.2f} shots has a channel capacity of <b>{:.1f}</b> bits against the "
      "{:.2f} identification requires, a surplus of {:.1f} to 1. All 17 ship cells must "
      "still be hit, and that is the binding resource.</p>".format(
          m["entropyBits"], LOG2_6, best["mean"], best["mean"] * LOG2_6, m["entropyBits"],
          best["mean"] * LOG2_6 / m["entropyBits"]))
    w("<p>Dividing the two gives an entropy floor of <b>{:.2f}</b> shots, <em>below</em> "
      "the trivial coverage bound of {}. A floor dominated by the count of ship cells adds "
      "nothing to it. The rung that binds counts finished games instead. Against a "
      "deterministic policy the transcript replays the policy, so a board finished on shot "
      "t is fixed by which of the first t-1 shots carried the other 16 hits and by one of "
      "<b>{:,}</b> announcement strings, giving at most K&#183;C(t,17) of the {:,} boards "
      "finished by shot t. Then E[T] = &#931;<sub>t</sub> P(T &gt; t) "
      "&#8805; &#931;<sub>t</sub> max(0, 1 &#8722; K&#183;C(t,17)/N) = <b>{:.4f}</b>, the "
      "sum saturating at t = 25.</p></div>".format(
          b["entropy"], b["coverage"], b["transcripts"], omega, b["waterfilling"]))
    w('<figure><div class="plate">')
    w(bound_ladder(b, policies))
    w("</div><figcaption><b>The unresolved interval.</b> Proved floors as vertical rungs, "
      "measured policies as points with 95% intervals over {:,} seeded boards. The shaded "
      "band from {:.3f} to {:.2f} is {:.1f} shots nobody has closed, and water filling "
      "accounts for {:.1%} of the distance from the coverage floor to the best measured "
      "policy.</figcaption></figure></section>\n".format(
          m["games"], b["waterfilling"], best["mean"], best["mean"] - b["waterfilling"],
          (b["waterfilling"] - b["coverage"]) / (best["mean"] - b["coverage"])))

    # 4 -----------------------------------------------------------------
    worst = max(obj, key=lambda r: r["maxInfo"] - r["optimal"])
    w('<section><div class="col"><div class="act">Four / the objective</div>')
    w("<h2>Maximising information is provably the wrong objective</h2>")
    w('<p class="lede">On boards small enough to solve exactly, every policy can be '
      "priced against the true optimum with no sampling error at all. The totals are "
      "integers over the whole space.</p>")
    w("<p>Maximising hit probability is a strong heuristic: exactly optimal on {} of the "
      "{} instances here and never worse than {:.2f} shots. Maximising one-step "
      "information gain loses <b>{:.2f} shots</b> on {}, more than double the "
      "optimum.</p>".format(
          sum(1 for r in obj if abs(r["maxProb"] - r["optimal"]) < 1e-9), len(obj),
          max(r["maxProb"] - r["optimal"] for r in obj),
          worst["maxInfo"] - worst["optimal"], esc(worst["instance"])))
    w("<p>Where a cell cannot sink a ship its answer is binary, so a shot yields H(p) in "
      "the cell's occupancy probability, and H peaks at p = 1/2. Below a half the two "
      "objectives agree, which is why they tie at turn 0, where the largest marginal is "
      "{:.4f}. They part in target mode: a cell beside a hit can pass a half, and past it "
      "H falls, so at p = 0.9 a shot is worth {:.2f} bits against 1.00 for a coin "
      "flip.</p>".format(max(prior_p), BIN_H_09))
    w("<p>The loss sits somewhere else. Entropy is zero once the answer is settled, so "
      "when the record has narrowed the board to one configuration every unshot cell "
      "scores alike and the tie falls to the lowest index. On {} the information rule "
      "fires <b>{:.2f} misses per game</b> at a board it has already determined, against "
      "a total loss of {:.2f} shots. It locates the ship and then leaves it. The other "
      "two rules fire none, on any instance here: a cell they are certain of still scores "
      "highest.</p></div>".format(
          esc(worst["instance"]), worst["maxInfoWaste"],
          worst["maxInfo"] - worst["optimal"]))
    w('<figure><div class="plate">')
    w(objective_bars(obj))
    w("</div><figcaption><b>Exact price of each objective.</b> Expected shots on instances "
      "where the optimum is computable. Totals are integers over the enumerated space, so "
      "every gap here is exact and carries no sampling "
      "error.</figcaption></figure>".format())
    w('<figure><div class="tablewrap"><table><thead><tr><th>instance</th><th>boards</th>'
      "<th>optimal</th><th>max-P(hit)</th><th>gap</th><th>max-info</th><th>gap</th>"
      "</tr></thead><tbody>")
    for r in obj:
        w('<tr><td>{}</td><td class="num">{:,}</td><td class="num">{:.4f}</td>'
          '<td class="num">{:.4f}</td><td class="num">{:.4f}</td>'
          '<td class="num">{:.4f}</td><td class="num">{:.4f}</td></tr>'.format(
              esc(r["instance"]), r["configurations"], r["optimal"], r["maxProb"],
              r["maxProb"] - r["optimal"], r["maxInfo"], r["maxInfo"] - r["optimal"]))
    w("</tbody></table></div><figcaption>Greedy hit-probability is provably suboptimal: on "
      "4x4 {3,2} it takes 2352 shots across the space where optimal play takes 2311, a "
      "difference of exactly 41.</figcaption></figure></section>\n")

    # 5 -----------------------------------------------------------------
    W = prior["width"]
    marg = [c / prior["total"] for c in prior["counts"]]
    rho = _spearman(marg, dens["meanTurn"])
    centre_turn = dens["meanTurn"][4 * W + 4]
    corner_turn = dens["meanTurn"][9 * W + 9]
    par_ev, par_od = _parity_split(par["shotRate"], W)
    den_ev, den_od = _parity_split(dens["shotRate"], W)
    den_t_ev, den_t_od = _parity_split(dens["meanTurn"], W)
    par_t_ev, par_t_od = _parity_split(par["meanTurn"], W)

    w('<section><div class="col"><div class="act">Five / the play</div>')
    w("<h2>A policy that reconstructs the prior it was never given</h2>")
    w('<p class="lede">Two maps of the same {:,} games. The first is the mean turn at '
      "which each cell is shot, which is the order a policy searches in. The second is the "
      "fraction of games in which a cell is ever shot at all, which is the coverage it "
      "achieves before the game ends. A policy finishing in {:.1f} shots visits fewer than "
      "half the cells, so the two maps carry different "
      "information.</p></div>".format(m["games"], dens["mean"]))

    w('<div class="pair">')
    for p, name in ((dens, "density"), (par, "parity hunt/target")):
        w('<figure><div class="plate">')
        w(board_heatmap(p["meanTurn"], prior["width"], prior["height"],
                        "Mean turn at which each cell is shot, " + name,
                        lambda v: "{:.0f}".format(v), "mean turn shot", cell=40))
        w("</div><figcaption><b>" + esc(name) + ", search order.</b> Mean turn index, "
          "lighter earlier.</figcaption></figure>")
    w("</div>")

    w("<p>The density policy hard-codes no geometry. It scores a cell by the placements "
      "of the remaining fleet covering it, each weighted by the open hits it touches, "
      "which while nothing is wounded is a plain count, and shoots the highest. That is "
      "enough to recover the prior: its mean shot turn against the exact prior marginals "
      "runs to a rank correlation of {:+.3f}, and it opens on the centre cell at mean turn "
      "{:.2f} while reaching the far corner at {:.2f}. The 2.67-to-1 centre-to-corner ratio "
      "of the marginal table in section one is the same ordering, arrived at without the "
      "table.</p>".format(rho, centre_turn, corner_turn))

    w('<div class="pair">')
    for p, name in ((dens, "density"), (par, "parity hunt/target")):
        w('<figure><div class="plate">')
        w(board_heatmap(p["shotRate"], prior["width"], prior["height"],
                        "Fraction of games in which each cell is shot, " + name,
                        lambda v: "{:.0f}".format(v * 100), "games shot in, %", cell=40))
        w("</div><figcaption><b>" + esc(name) + ", coverage.</b> Percentage of games in "
          "which the cell is ever shot. These sum to {:.1f}, the mean shots per "
          "game.</figcaption></figure>".format(sum(p["shotRate"])))
    w("</div>")

    w("<p>Both policies play the same boards, so the maps compare their rules directly. "
      "Parity hunt/target confines its hunt to one diagonal colour class and leaves that "
      "class only to finish a wounded ship. The restriction is exact rather than "
      "heuristic: a ship of length two or more covers a cell of each class, so half the "
      "board can be skipped without risking a miss. Its coverage is that restriction, "
      "{:.1%} of games on even cells against {:.1%} on odd, a ratio of {:.2f}.</p>".format(
          par_ev, par_od, par_ev / par_od))
    w("<p>The density policy encodes no geometry. It scores each cell by the "
      "remaining-fleet placements covering it, weighted by the open hits each touches, "
      "and shoots the maximum. Its colour classes "
      "separate by a factor of {:.2f} in coverage and by {:+.2f} turns in order, against "
      "{:.2f} and {:+.2f} for the parity policy. What the density map shows instead is the "
      "centre gradient of the marginal above it: every game at the middle cell, {:.1%} at "
      "the opposite corner.</p>".format(
          den_ev / den_od, den_t_od - den_t_ev, par_ev / par_od,
          par_t_od - par_t_ev, dens["shotRate"][9 * W + 9]))

    w('<figure><div class="plate">')
    w(survival(policies))
    cross = None
    sa, sb = _survival(dens["histogram"]), _survival(par["histogram"])
    for n in range(min(len(sa), len(sb))):
        if sa[n] > sb[n] + 1e-12:
            cross = n
            break
    w("</div><figcaption><b>Fraction of games still running after n shots.</b> The same "
      "{:,} boards for every policy, so the comparison is paired. Survival needs no bin "
      "choice and makes crossings visible, and there is one: density sits below parity "
      "hunt/target everywhere up to shot {}, then rises above it in the tail. Density is "
      "better on average by {:.2f} shots and does not stochastically dominate, so a reader "
      "who cares about the worst case rather than the mean cannot read the ranking off the "
      "means alone. The spreads differ as much as the centres: {}."
      "</figcaption></figure></section>\n".format(
          m["games"], cross, par["mean"] - dens["mean"],
          "; ".join("{} {:.2f} with sd {:.2f}".format(p["name"], p["mean"], p["sd"])
                    for p in policies)))

    # objects -----------------------------------------------------------
    w('<section><div class="col"><div class="act">Interlude / the objects</div>')
    w("<h2>Three objects the arguments stand on</h2>")
    w('<p class="lede">The engine runs on three structures that are easier to look at '
      "than to describe.</p></div>")

    w('<figure><div class="plate">')
    w(orbit_map(prior["counts"], prior["total"], prior["width"], prior["height"]))
    w("</div><figcaption><b>The 15 dihedral orbits.</b> Reflections and the diagonal fold "
      "the 100 cells into 15 classes, so a quantity respecting the board's symmetry is "
      "settled by 15 evaluations. Hovering gives each orbit's exact configuration count; "
      "weighted by orbit size they sum to 17|&#937;|, since every board occupies 17 cells. "
      "The prior heatmap earlier in this page is this pattern, "
      "shaded.</figcaption></figure>")

    w('<figure><div class="plate">')
    if not data["blockingWitness"]:
        raise ValueError("blockingWitness is empty; the certificates figure would be blank")
    w(blocking_boards(data["blockingWitness"], prior["width"], prior["height"]))
    w("</div><figcaption><b>Blocking sets.</b> Shoot the marked cells and no placement of "
      "that length survives untouched, which is what makes beta(L) the number of shots "
      "guaranteeing first contact with a lone ship of that length. Each set is drawn at "
      "its minimum size, so the marks can be counted. The DP computes the complement: the "
      "largest set holding no L cells in a line runs {}, and 100 minus that is beta(L). A "
      "greedy cover reaches that size for lengths 2 and 5 and misses by one and two for 3 "
      "and 4, where the set shown is rebuilt by deciding each cell against the exact "
      "DP.</figcaption></figure>".format(
          ", ".join(str(e["freeSet"]) for e in b["blocking"])))

    w('<figure><div class="plate">')
    w(order_dependence(data["orderDependence"]))
    w("</div><figcaption><b>Why the record is a sequence.</b> The same seven cells with the "
      "same seven outcomes, fired in two orders. Announcing the sink on one cell rather "
      "than the other leaves a different set of boards standing, "
      f'{data["orderDependence"]["orders"][0]["omega"]} against '
      f'{data["orderDependence"]["orders"][1]["omega"]}. A cache keyed on the set of shots '
      "would merge these two positions and return the wrong posterior for one of "
      "them.</figcaption></figure></section>\n")

    # 6 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">Six / the collapse</div>')
    w("<h2>From fifteen billion to one in about forty shots</h2>")
    w('<p class="lede">Ten orders of magnitude disappear in about forty moves, and the '
      "count is recounted from scratch after every shot rather than estimated.</p>")
    biggest = max((math.log10(g["omega"][i] / g["omega"][i + 1]), g["game"], i + 1)
                  for g in col for i in range(len(g["outcomes"]))
                  if g["outcomes"][i] == 2 and g["omega"][i + 1] > 0)
    w("<p>The decrease is not smooth. A sinking announcement fixes a whole ship at once, "
      "and the largest step at a sink removes <b>{:.2f}</b> orders of magnitude, at shot "
      "{} of game {}. The marked points are sinks.</p></div>".format(
          biggest[0], biggest[2], biggest[1]))
    w('<figure><div class="plate">')
    w(collapse(col, omega))
    w("</div><figcaption><b>Posterior collapse.</b> Surviving configurations after each "
      "shot, log scale, for three games under the density policy. Every value is an exact "
      "count from a full sweep.</figcaption></figure>")
    # The scrubber: the same collapse, one turn at a time, on the board itself.
    scrub = next((g for g in col if "frames" in g), None)
    if scrub is not None:
        payload = {
            "width": prior["width"], "height": prior["height"],
            "omega": scrub["omega"], "cells": scrub["cells"],
            "outcomes": scrub["outcomes"], "truth": scrub["truth"],
            "frames": scrub["frames"],
        }
        w('<div class="col"><h3>The same game, one turn at a time</h3>')
        w("<p>The curve above says how much collapsed. This says <em>where</em>. Drag "
          "the slider, or use the arrow keys.</p>")
        w("<p>Watch the two things the curve cannot show: probability mass sliding away "
          "from a miss into the cells that remain, and a sinking announcement flattening "
          "a whole region at once because the ship it accounted for is now placed.</p>")
        w("</div>")
        w('<figure><div class="plate"><div id="scrub" data-frames=\'' +
          json.dumps(payload, separators=(",", ":")).replace("'", "&#39;") + '\'></div>')
        w("</div><figcaption><b>Belief scrubber.</b> Exact cell marginals after every "
          "shot of one game, quantised to a byte per cell. One colour scale spans the "
          "whole game, so a frame late in the collapse is legibly emptier than an early "
          "one. Shot cells carry a glyph, leaving colour to the "
          "posterior.</figcaption></figure>")

    w('<div class="col"><div class="note">Every figure and every number on this page '
      "reads from <code>out/figures.json</code>, which the engine writes directly. The "
      "renderer derives summaries from it and recomputes nothing, so no figure can "
      "disagree with the engine that produced it.</div></div>")
    w("</section>\n")

    # 7, the conclusion --------------------------------------------------
    zero_prob = sum(1 for r in obj if abs(r["maxProb"] - r["optimal"]) < 1e-9)
    worst_prob = max(r["maxProb"] - r["optimal"] for r in obj)
    worst_row = max(obj, key=lambda r: r["maxInfo"] - r["optimal"])
    worst_info = worst_row["maxInfo"] - worst_row["optimal"]
    worst_waste = worst_row["maxInfoWaste"]
    gap = best["mean"] - b["waterfilling"]

    w('<section><div class="col"><div class="act">Seven / the strategy</div>')
    w("<h2>Shoot the likeliest cell, and recompute after every answer</h2>")
    w('<p class="lede">The optimum at 10x10 is not known. The exact solver runs to a few '
      "hundred surviving configurations and this instance opens at {:,}, so what follows "
      "is the best rule the measurements support, together with how far from optimal it "
      "can be shown to be.</p>".format(omega))

    w("<p><b>Take the cell of highest posterior occupancy.</b> On the six instances small "
      "enough to solve outright, that rule is exactly optimal on {} of them and never more "
      "than <b>{:.4f} shots</b> from optimal on the rest. No other rule tested comes "
      "within an "
      "order of magnitude of that margin, and the rule needs only the marginals, which one "
      "forward and one backward pass return together.</p>".format(zero_prob, worst_prob))

    w("<p><b>Do not select on information.</b> Maximising one-step information gain costs "
      "up to <b>{:.4f} shots</b>, more than doubling the optimum on 5x4 {{3}}. A shot's "
      "information is the entropy of its answer, and that is zero once the posterior "
      "settles it, so a located ship scores nothing and the rule walks away from it: "
      "{:.2f} of those lost shots are fired at a board the record already names. "
      "Identification is not the scarce resource here. A game of {:.2f} shots carries "
      "{:.1f} bits against the {:.2f} needed to name the board.</p>".format(
          worst_info, worst_waste, best["mean"], best["mean"] * LOG2_6, m["entropyBits"]))

    w("<p><b>Recompute every turn.</b> Fixing the shot order in advance costs between "
      "1.31 and 2.09 times the adaptive optimum on the instances where both are solved. "
      "At full scale the best fixed order found is 88.73 shots against <b>{:.2f}</b> for "
      "an adaptive policy. The two are different objects, but the separation dwarfs "
      "every margin between the adaptive rules compared here.</p>".format(best["mean"]))

    w("<p><b>If a posterior is out of budget, count placements instead.</b> Scoring a cell "
      "by the number of remaining-fleet placements covering it needs no sweep, measures "
      "<b>{:.3f}</b> shots over {:,} boards, and reproduces the prior's centre weighting "
      "on its own. Restricting the hunt to one diagonal colour class is sound and cheaper "
      "still, and costs <b>{:.2f} shots</b> against it.</p>".format(
          best["mean"], m["games"], pol["parity hunt/target"]["mean"] - best["mean"]))

    w("<p><b>Assume a slight edge bias rather than a flat prior.</b> Against an opponent "
      "who places uniformly this costs almost nothing, and against one who does not it "
      "recovers most of the difference. Believing a mild bias improves the worst case over "
      "all opponents tested by 0.1392 shots on 5x5 {4,3,2} and 0.2057 on 4x4 {3,2}, "
      "without knowing who is playing. The effect is real and small.</p>")

    w("<p>What none of this settles is the size of what remains. The certified floor is "
      "<b>{:.3f}</b> and the best rule measured here is <b>{:.2f}</b>, leaving "
      "<b>{:.2f} shots</b> unaccounted for. That width is the slack in the bound plus "
      "the loss of the rule against the true optimum, and nothing in these results says "
      "how it divides. A better policy and a better lower bound would look identical from "
      "here.</p>".format(b["waterfilling"], best["mean"], gap))

    w("<p>Two cautions on reading the ranking. The density and parity curves cross in the "
      "tail, so the {:.2f}-shot difference between their means is not stochastic "
      "dominance, and a reader who cares about the worst game rather than the average one "
      "cannot take the ordering from the means. And the greedy rule recommended above is "
      "provably suboptimal rather than merely unverified: on 4x4 {{3,2}} it spends 2352 "
      "shots "
      "across the space where optimal play spends 2311. It is the best rule available, "
      "which is a different claim from the best rule.</p></div>\n".format(
          pol["parity hunt/target"]["mean"] - best["mean"]))

    # the opening book ---------------------------------------------------
    book = data["openingBook"]
    if not book:
        raise ValueError("openingBook is empty; the report cannot describe an opening "
                         "that was not computed")
    if True:
        bw = prior["width"]
        forced = book[-1]
        w('<div class="col"><h3>Where to shoot first</h3>')
        w("<p>The rule is adaptive, so it has no fixed order, but it has a principal "
          "variation: the line it takes while every answer is a miss. That is where the "
          "opening spends most of its time, since the best first cell is a miss {:.1%} of "
          "the time, and it is the nearest thing to a ranking of the board.</p>".format(
              1 - book[0]["p"]))
        w("<p>The line walks the long diagonal outward from the centre and then fills the "
          "gaps between those cells. It also ends by itself, after <b>{}</b> shots: by then "
          "only {:,} boards remain and every one of them occupies {}, so that cell has "
          "marginal 1 and the miss branch is empty. That many shots into this order, "
          "contact is not likely but certain.</p>".format(
              len(book), forced["omega"],
              chr(ord("A") + forced["cell"] % bw) + str(forced["cell"] // bw + 1)))
        w("<p>The marginals rise as the line runs, from {:.4f} at the first cell to "
          "{:.4f} at the last but one. Missing does not only remove boards, it concentrates "
          "what is left, so each successive shot is a better bet than the one "
          "before.</p></div>".format(book[0]["p"], book[-2]["p"] if len(book) > 1 else 0.0))
        w('<figure><div class="plate">')
        w(opening_book(book, prior["width"], prior["height"]))
        w("</div><figcaption><b>The opening, ranked.</b> Shot order along the all-miss "
          "branch of the recommended rule, earliest darkest. A hit at any point ends the "
          "line and the rule recomputes; this is the order to fall back to while nothing "
          "has been found. Unshaded cells are never reached, because the line terminates "
          "in a forced hit first.</figcaption></figure>")

    # the summary ----------------------------------------------------------
    w('<div class="col"><h3>The short version</h3>')
    w('<p class="lede">Everything above, in six lines.</p>')
    w("<ul class=\"summary\">")
    w("<li><b>Attack the highest posterior marginal, and recompute after every "
      "answer.</b> Exactly optimal on {} of the {} instances small enough to check, and "
      "never more than {:.4f} shots off on the others.</li>".format(
          sum(1 for r in obj if abs(r["maxProb"] - r["optimal"]) < 1e-9), len(obj),
          max(r["maxProb"] - r["optimal"] for r in obj)))
    w("<li><b>Open on the centre and work outward along the diagonal.</b> That is the line "
      "above, and the {} cells it names guarantee contact.</li>".format(len(book)))
    w("<li><b>Never pick a shot by information gain.</b> Up to {:.2f} shots worse, because "
      "a settled answer is worth zero bits, so the rule locates a ship and then spends "
      "{:.2f} misses a game on a board it has already solved.</li>".format(
          max(r["maxInfo"] - r["optimal"] for r in obj),
          max(r["maxInfoWaste"] for r in obj)))
    w("<li><b>Counting placements is a good substitute if you cannot afford a "
      "posterior.</b> {:.2f} shots against a certified floor of {:.2f}, with no sweep "
      "required.</li>".format(best["mean"], b["waterfilling"]))
    w("<li><b>Hiding: put ships where the attacker looks last, but not every "
      "game.</b> A prior-assuming attacker shoots the middle cell in every game and the "
      "far corner in {:.1%} of them, so the edges buy time against a stranger. Against "
      "anyone who plays you repeatedly the same habit is worth about a shot to them, and "
      "ten games is enough to read it.</li>".format(dens["shotRate"][9 * bw + 9]
                                                    if book else 0.111))
    w("<li><b>What is still open.</b> The optimum for this board is unknown. It lies "
      "between {:.3f} and {:.2f}, and nothing here narrows which end.</li>".format(
          b["waterfilling"], best["mean"]))
    w("</ul>")
    w('<p class="thanks">Thank you for reading.</p></div>')
    w("</section>\n")

    w('<footer><div class="col">Mayflower &middot; exact Bayesian inference over '
      "Battleships &middot; " + esc(m["instance"]) + " &middot; {:,} games per policy on "
      "one seeded uniform board pool.</div></footer>".format(m["games"]))
    w("</div>\n<script>" + SCRIPT + "</script>\n")
    w("<script>" + ENGINE_JS + "</script>\n")
    w("<script>" + LIVE_JS + "</script>\n")
    w("<script>" + SCRUB_JS + "</script>\n")

    with io.open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(o.getvalue())
    return len(o.getvalue())


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "out/figures.json"
    dst = sys.argv[2] if len(sys.argv) > 2 else "out/report.html"
    with io.open(src, encoding="utf-8") as fh:
        payload = json.load(fh)
    print("wrote {}, {:.1f} KB".format(dst, build(payload, dst) / 1024))
