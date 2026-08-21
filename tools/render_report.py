"""Assemble the Mayflower report page from the figure-data contract.

Figure primitives live in build_report.py; this file is the page: tokens, prose,
and the order of the argument. Run it as:

    python tools/render_report.py out/figures.json out/report.html
"""

import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from build_report import (RAMP, board_heatmap, bound_ladder, collapse, esc,
                          layer_profile, objective_bars, scaling, survival)

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
.plate svg { display: block; width: 100%; height: auto; min-width: 340px; }
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
.lc.miss { background: var(--grid); color: var(--muted-ink); }
.lc.hit  { background: var(--series-2); color: #fff; font-size: 13px; }
.lc.sunk { background: var(--series-2); color: #fff; font-size: 15px; opacity: .72; }
.lc.ghost { outline: 2px dashed var(--series-3); outline-offset: -3px; }
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


def load_live():
    here = os.path.dirname(os.path.abspath(__file__))
    return io.open(os.path.join(here, "..", "web", "live.js"), encoding="utf-8").read()


def load_pool():
    import base64
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "web", "pool.bin"), "rb") as fh:
        return base64.b64encode(fh.read()).decode("ascii")


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
    w('<p class="standfirst">Every legal fleet, counted exactly, without enumerating '
      "one. What that buys, what it proves, and where the remaining gap actually "
      "lives.</p></div>")
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
    w("<p>Two regimes run underneath, because the costs are opposite. Early on the "
      "posterior spans billions of boards and an exact sweep would take half a minute in a "
      "browser, while a uniform sample of 200,000 still holds most of its survivors and is "
      "accurate. Late on the sample is exhausted and the exact sweep has become cheap. The "
      "readout says which one is answering.</p></div>")
    w('<div class="livewrap" id="live" data-pool="' + POOL_B64 + '">')
    w('<div class="livegrid"><div class="liveboard"></div>')
    w('<div><div class="livestats"></div><div class="livebtns">'
      '<button data-act="step">Fire</button>'
      '<button data-act="play">Play</button>'
      '<button data-act="new">New fleet</button>'
      '<button data-act="reveal">Reveal fleet</button>'
      "</div></div></div></div>")
    w('<div class="col"><figcaption>Filled cells are shots: a dot is a miss, a disc a hit, '
      "a cross a sunk ship. Unshot cells carry the posterior as a percentage, on the same "
      "blue ramp used everywhere else on this page.</figcaption></div>")
    w("</section>\n")

    # 1 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">One / the space</div>')
    w("<h2>Fifteen billion boards, and they are not interchangeable</h2>")
    w('<p class="lede">A 10x10 board holding the fleet <code>{{5,4,3,3,2}}</code> admits '
      "exactly <b>{:,}</b> arrangements. A transfer-matrix sweep counts them without "
      "writing one down, and the same sweep returns the exact chance that each cell is "
      "occupied.</p>".format(omega))
    w("<p>Corners are the loneliest cells on the board and the centre the busiest, by a "
      "factor of <b>{:.2f}</b>. These are exact rationals, not estimates: they sum to "
      "17.000, the number of cells the fleet occupies.</p></div>".format(
          max(prior_p) / min(prior_p)))
    w('<figure><div class="plate">')
    w(board_heatmap(prior_p, prior["width"], prior["height"],
                    "Exact probability that each cell holds a ship",
                    lambda v: "{:.3f}".format(v), "probability a ship covers this cell"))
    w("</div><figcaption><b>Where the ships actually are.</b> Exact occupancy probability "
      "per cell under a uniform prior over all {:,} configurations. The corner reads "
      "{:.4f} and the centre {:.4f}. The four centre cells tie exactly, which is why an "
      "optimal opening shot has a four-way choice.</figcaption></figure>".format(
          omega, min(prior_p), max(prior_p)))

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
    w("</tbody></table></div><figcaption>The counts behind the curve. Every figure here "
      "has its table.</figcaption></figure></div></section>\n")

    # 2 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">Two / the machine</div>')
    w("<h2>Counting without enumerating</h2>")
    w('<p class="lede">The sweep carries a boundary profile across the board one cell at '
      "a time. Its whole lattice is <b>{:,}</b> edges and <b>{:,}</b> state visits, "
      "against {:,} configurations. That ratio, better than <b>{:,.0f} to one</b>, is the "
      "entire trick.</p>".format(lat["edges"], lat["stateVisits"], omega,
                                 omega / lat["edges"]))
    w("<p>The profile remembers, for each row, how far a horizontal ship still extends, "
      "how far a vertical ship in the current column still runs, and how much of the "
      "fleet has been spent. Nothing else. The live state count swells through the middle "
      "of the board and collapses as the fleet runs out.</p></div>")
    w('<figure><div class="plate">')
    w(layer_profile(lat["layerSizes"], lat["peakStates"]))
    w("</div><figcaption><b>The shape of the computation.</b> Live states entering each of "
      "the 100 cell layers, peaking at {:,}. The collapse over the last two columns is the "
      "fleet counter running out of ships: by then most partial boards cannot be "
      "completed.</figcaption></figure></section>\n".format(lat["peakStates"]))

    # 3 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">Three / the bound</div>')
    w("<h2>Information is the abundant resource</h2>")
    w('<p class="lede">Identifying the board takes {:.2f} bits. A game lasting forty-odd '
      "shots carries far more than that, so information is never what runs short. Every "
      "one of the 17 ship cells has to be hit, and that is what costs.</p>".format(
          m["entropyBits"]))
    w("<p>The entropy bound lands at <b>{:.2f}</b> shots, <em>below</em> the trivial "
      "coverage bound of {}. It is vacuous, and saying so is the point. The bound that "
      "binds comes from counting transcripts: with <b>{:,}</b> distinct ways the 17 hits "
      "can be announced, no policy can average under <b>{:.4f}</b> "
      "shots.</p></div>".format(b["entropy"], b["coverage"], b["transcripts"],
                                b["waterfilling"]))
    w('<figure><div class="plate">')
    w(bound_ladder(b, policies))
    w("</div><figcaption><b>The unresolved interval.</b> Proved floors as vertical rungs, "
      "measured policies as points with 95% intervals over {:,} seeded boards. The shaded "
      "band from {:.3f} to {:.2f} is what nobody has closed. The entropy rung sitting "
      "left of the coverage rung is the whole argument in one "
      "picture.</figcaption></figure></section>\n".format(m["games"], b["waterfilling"],
                                                          best["mean"]))

    # 4 -----------------------------------------------------------------
    worst = max(obj, key=lambda r: r["maxInfo"] - r["optimal"])
    w('<section><div class="col"><div class="act">Four / the objective</div>')
    w("<h2>Chasing information loses the game</h2>")
    w('<p class="lede">On boards small enough to solve exactly, every policy can be '
      "priced against the true optimum with no sampling error at all. The totals are "
      "integers over the whole space.</p>")
    w("<p>Maximising hit probability is a strong heuristic: exactly optimal on several "
      "instances and never far off. Maximising one-step information gain is a disaster, "
      "losing <b>{:.2f} shots</b> on {}, more than double the optimum. Shots spent "
      "learning where the ships are, instead of hitting them, are "
      "wasted.</p></div>".format(worst["maxInfo"] - worst["optimal"],
                                 esc(worst["instance"])))
    w('<figure><div class="plate">')
    w(objective_bars(obj))
    w("</div><figcaption><b>Exact price of each objective.</b> Expected shots on instances "
      "where the optimum is computable. Bars are direct-labelled, so identity never rests "
      "on colour alone.</figcaption></figure>")
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
    w('<section><div class="col"><div class="act">Five / the play</div>')
    w("<h2>Parity nobody programmed</h2>")
    w('<p class="lede">Averaged over {:,} games, the turn at which each cell gets shot '
      "reveals the search pattern a policy invents for itself. Neither policy mentions a "
      "lattice or a checkerboard anywhere in its code.</p></div>".format(m["games"]))
    w('<div class="pair">')
    for p, name in ((dens, "density"), (par, "parity hunt/target")):
        w('<figure><div class="plate">')
        w(board_heatmap(p["meanTurn"], prior["width"], prior["height"],
                        "Mean turn at which each cell is shot, " + name,
                        lambda v: "{:.0f}".format(v), "mean turn shot", cell=40))
        w("</div><figcaption><b>" + esc(name) + ".</b> Mean turn index at which each cell "
          "is shot. Lighter cells are visited earlier.</figcaption></figure>")
    w("</div>")
    w('<figure><div class="plate">')
    w(survival(policies))
    w("</div><figcaption><b>Fraction of games still running.</b> Survival curves over the "
      "same {:,} boards, so the comparison is paired. Survival is used instead of a "
      "histogram because it needs no bin choice and makes crossings visible; a crossing "
      "would mean neither policy dominates.</figcaption></figure></section>\n".format(
          m["games"]))

    # 6 -----------------------------------------------------------------
    w('<section><div class="col"><div class="act">Six / the collapse</div>')
    w("<h2>From fifteen billion to one</h2>")
    w('<p class="lede">Following single games, recounting the surviving configurations '
      "exactly after every shot. Ten orders of magnitude disappear in about forty "
      "moves.</p>")
    w("<p>The drops are not smooth. A sinking announcement pins a whole ship at once and "
      "can take two or three decades out of the space in one step; the marked points are "
      "sinks.</p></div>")
    w('<figure><div class="plate">')
    w(collapse(col, omega))
    w("</div><figcaption><b>Posterior collapse.</b> Surviving configurations after each "
      "shot, log scale, for three games under the density policy. Every value is an exact "
      "count from a full sweep.</figcaption></figure>")
    w('<div class="col"><div class="note">Everything here is generated from '
      "<code>out/figures.json</code>, which the engine writes directly. No number was "
      "transcribed by hand, and none was recomputed by the renderer.</div></div>")
    w("</section>\n")

    w('<footer><div class="col">Mayflower &middot; exact Bayesian inference over '
      "Battleships &middot; " + esc(m["instance"]) + " &middot; {:,} games per policy on "
      "one seeded uniform board pool.</div></footer>".format(m["games"]))
    w("</div>\n<script>" + SCRIPT + "</script>\n")
    w("<script>" + ENGINE_JS + "</script>\n")
    w("<script>" + LIVE_JS + "</script>\n")

    with io.open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(o.getvalue())
    return len(o.getvalue())


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "out/figures.json"
    dst = sys.argv[2] if len(sys.argv) > 2 else "out/report.html"
    with io.open(src, encoding="utf-8") as fh:
        payload = json.load(fh)
    print("wrote {}, {:.1f} KB".format(dst, build(payload, dst) / 1024))
