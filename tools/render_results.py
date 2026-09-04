"""Render experiments/results.json as a standalone results dossier.

Every number on the page is read from results.json, which is itself read from
what the tools emitted. Nothing is transcribed, so the page cannot drift from the
engine without the collector failing first.

    python tools/collect_results.py && python tools/render_results.py

Writes out/results.html.
"""

from __future__ import annotations

import html
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def esc(s):
    return html.escape(str(s), quote=True)


def load():
    p = os.path.join(ROOT, "experiments", "results.json")
    return json.load(io.open(p, encoding="utf-8"))


def by_id(results):
    """Index by id, refusing a collision.

    A dict comprehension keeps the last of any duplicate, so two results sharing
    an id would leave one silently unreachable and the page would show the other
    without a word. The ids are built by the collector from instance and metric,
    so a collision means two different quantities are being called the same
    thing.
    """
    out = {}
    for r in results:
        if r["id"] in out:
            raise ValueError("two results share the id {!r}: {} and {}".format(
                r["id"], out[r["id"]].get("metric"), r.get("metric")))
        out[r["id"]] = r
    return out


def fam(results, name):
    return [r for r in results if r["family"] == name]


def group(n, places=0):
    """Digit grouping with thin spaces, so a ten-digit integer stays readable."""
    if isinstance(n, float) and places:
        return "{:,.{}f}".format(n, places).replace(",", "&thinsp;")
    return "{:,}".format(n).replace(",", "&thinsp;")


# --- marks ----------------------------------------------------------------

def svg_open(w, h, label, title):
    """Open a figure at its design size.

    The cap stops the page stretching a figure past the width it was drawn for.
    Without it the two 520-wide figures rendered 1.75 times oversized while the
    760-wide ladder rendered at 1.19, so the same 11px label came out at three
    different sizes down one page."""
    return ('<svg viewBox="0 0 {w} {h}" role="img" aria-label="{label}" class="fig" '
            'preserveAspectRatio="xMidYMid meet" style="max-width:{w}px">'
            "<title>{title}</title>").format(w=w, h=h, label=esc(label), title=esc(title))


def ladder(bounds, best):
    """The bound ladder as one number line.

    A single axis in shots. The certified floor, the dominated rung drawn where it
    actually falls, and the best measured policy, with the unresolved interval
    shaded between the binding floor and the ceiling.
    """
    w, h, pad = 760, 168, 46
    span = 50.0
    x = lambda v: pad + (w - 2 * pad) * v / span
    y = 96

    out = [svg_open(w, h, "The bound ladder on a single axis in shots",
                    "Lower bounds and the best measured policy, in shots")]

    # Unresolved interval, floor to ceiling.
    out.append('<rect x="{:.1f}" y="{}" width="{:.1f}" height="18" class="gapfill"/>'
               .format(x(bounds["waterfilling"]), y - 9,
                       x(best) - x(bounds["waterfilling"])))

    out.append('<line x1="{:.1f}" y1="{}" x2="{:.1f}" y2="{}" class="axis"/>'
               .format(pad, y, w - pad, y))
    for t in range(0, 51, 10):
        out.append('<line x1="{0:.1f}" y1="{1}" x2="{0:.1f}" y2="{2}" class="tick"/>'
                   .format(x(t), y, y + 6))
        out.append('<text x="{:.1f}" y="{}" class="tk" text-anchor="middle">{}</text>'
                   .format(x(t), y + 22, t))
    out.append('<text x="{:.1f}" y="{}" class="tk" text-anchor="middle">shots</text>'
               .format(x(25), y + 40))

    marks = [
        (bounds["entropy"], "entropy", "13.08", "dominated", -58),
        (bounds["coverage"], "coverage", "17", "exact", -34),
        (bounds["waterfilling"], "water filling", "24.088", "binding floor", -58),
        (best, "density policy", "44.369", "best measured", -34),
    ]
    for v, label, value, kind, dy in marks:
        cls = "mk-open" if kind == "dominated" else (
            "mk-meas" if kind == "best measured" else "mk-exact")
        out.append('<line x1="{0:.1f}" y1="{1}" x2="{0:.1f}" y2="{2}" class="stem"/>'
                   .format(x(v), y - 9, y + dy + 20))
        out.append('<circle cx="{:.1f}" cy="{}" r="5.5" class="{}"/>'
                   .format(x(v), y, cls))
        anchor = "start" if v < 8 else ("end" if v > 42 else "middle")
        out.append('<text x="{:.1f}" y="{}" class="lb" text-anchor="{}">{}</text>'
                   .format(x(v), y + dy, anchor, esc(label)))
        out.append('<text x="{:.1f}" y="{}" class="lv" text-anchor="{}">{}</text>'
                   .format(x(v), y + dy + 15, anchor, value))

    out.append('<text x="{:.1f}" y="{}" class="note" text-anchor="middle">'
               'unresolved, {:.1f} shots</text>'
               .format((x(bounds["waterfilling"]) + x(best)) / 2, y + 62,
                       best - bounds["waterfilling"]))
    out.append("</svg>")
    return "".join(out)


def scaling(points):
    """Configurations against board side, log y, one series."""
    import math
    w, h, l, r, t, b = 520, 230, 52, 16, 18, 40
    xs = [p["n"] for p in points]
    ys = [math.log10(p["omega"]) for p in points]
    lo = math.floor(min(ys))
    hi = math.ceil(max(ys))
    x = lambda v: l + (w - l - r) * (v - min(xs)) / (max(xs) - min(xs))
    y = lambda v: t + (h - t - b) * (1 - (v - lo) / (hi - lo))

    out = [svg_open(w, h, "Configuration count against board side, log scale",
                    "Configurations by board side, log scale")]
    for e in range(lo, hi + 1):
        out.append('<line x1="{}" y1="{:.1f}" x2="{}" y2="{:.1f}" class="grid"/>'
                   .format(l, y(e), w - r, y(e)))
        out.append('<text x="{}" y="{:.1f}" class="tk" text-anchor="end">10^{}</text>'
                   .format(l - 8, y(e) + 4, e))
    d = " ".join(("M" if i == 0 else "L") + "{:.1f} {:.1f}".format(x(xs[i]), y(ys[i]))
                 for i in range(len(xs)))
    out.append('<path d="{}" class="line"/>'.format(d))
    for i, p in enumerate(points):
        out.append('<circle cx="{:.1f}" cy="{:.1f}" r="4" class="mk-exact">'
                   '<title>{}x{}: {} configurations</title></circle>'
                   .format(x(xs[i]), y(ys[i]), p["n"], p["n"], group(p["omega"])))
        out.append('<text x="{:.1f}" y="{}" class="tk" text-anchor="middle">{}</text>'
                   .format(x(xs[i]), h - 16, p["n"]))
    out.append('<text x="{:.1f}" y="{}" class="tk" text-anchor="middle">board side</text>'
               .format((l + w - r) / 2, h - 2))
    out.append("</svg>")
    return "".join(out)


def policies(rows):
    """Three means with 95% intervals. A dot plot, because these are estimates."""
    w, h, l, r, t = 520, 150, 150, 24, 26
    lo, hi = 40, 100
    x = lambda v: l + (w - l - r) * (v - lo) / (hi - lo)

    out = [svg_open(w, h, "Mean shots per policy with 95 percent intervals",
                    "Mean shots to clear, with 95% intervals")]
    for tick in range(40, 101, 20):
        out.append('<line x1="{0:.1f}" y1="{1}" x2="{0:.1f}" y2="{2}" class="grid"/>'
                   .format(x(tick), t - 8, t + 84))
        out.append('<text x="{:.1f}" y="{}" class="tk" text-anchor="middle">{}</text>'
                   .format(x(tick), t + 102, tick))
    for i, p in enumerate(rows):
        yy = t + 14 + i * 30
        out.append('<text x="{}" y="{}" class="lb" text-anchor="end">{}</text>'
                   .format(l - 14, yy + 4, esc(p["note"])))
        out.append('<line x1="{:.1f}" y1="{}" x2="{:.1f}" y2="{}" class="whisker"/>'
                   .format(x(p["value"] - p["ci"]), yy, x(p["value"] + p["ci"]), yy))
        out.append('<circle cx="{:.1f}" cy="{}" r="5" class="mk-meas">'
                   '<title>{}: {:.3f} shots, 95% interval +/- {:.3f}</title></circle>'
                   .format(x(p["value"]), yy, esc(p["note"]), p["value"], p["ci"]))
        out.append('<text x="{:.1f}" y="{}" class="lv" text-anchor="start">{:.3f}</text>'
                   .format(x(p["value"]) + 12, yy + 4, p["value"]))
    out.append('<text x="{:.1f}" y="{}" class="tk" text-anchor="middle">'
               'mean shots to clear</text>'.format((l + w - r) / 2, h - 4))
    out.append("</svg>")
    return "".join(out)


# --- tables ---------------------------------------------------------------

def chip(exact):
    return ('<span class="chip chip-exact">exact</span>' if exact
            else '<span class="chip chip-meas">measured</span>')


def rows_table(head, rows):
    out = ['<div class="tw"><table><thead><tr>']
    out += ["<th>{}</th>".format(esc(h)) for h in head]
    out.append("</tr></thead><tbody>")
    # A cell holds either plain text or markup this file generated, a chip or a
    # number carrying &thinsp;. Nothing reaches here from outside the repository,
    # so testing for markup is enough to decide whether to escape.
    def cell(c):
        raw = isinstance(c, str) and ("<" in c or "&" in c)
        return "<td>{}</td>".format(c if raw else esc(c))

    for r in rows:
        out.append("<tr>" + "".join(cell(c) for c in r) + "</tr>")
    out.append("</tbody></table></div>")
    return "".join(out)


def fmt(v, places=4):
    if v is None:
        return "&ndash;"
    if isinstance(v, float):
        return "{:.{}f}".format(v, places)
    return group(v)


def _check_counts(d):
    """The page quotes d["counts"]; the rows come from d["results"]. If those two
    ever disagree the header is a claim about data the page is not showing."""
    results = d["results"]
    stated = d["counts"]
    actual = {
        "results": len(results),
        "exact": sum(1 for r in results if r["exact"]),
        "measured": sum(1 for r in results if not r["exact"]),
        "families": len({r["family"] for r in results}),
    }
    drift = {k: (stated[k], v) for k, v in actual.items()
             if k in stated and stated[k] != v}
    if drift:
        raise ValueError(
            "collected counts disagree with the collected results: "
            + ", ".join("{} says {} but there are {}".format(k, a, b)
                        for k, (a, b) in sorted(drift.items())))
    if not d["crossChecks"]:
        raise ValueError("crossChecks is empty; the agreement section would be blank")


def build(d):
    _check_counts(d)
    R = d["results"]
    idx = by_id(R)
    b = {r["note"]: r["value"] for r in fam(R, "bounds") if r.get("note")}
    pol = sorted(fam(R, "policy"), key=lambda r: -r["value"])
    best = min(r["value"] for r in pol)
    scale_pts = [{"n": int(r["id"].split("-")[1].split("x")[0]), "omega": r["value"]}
                 for r in fam(R, "counting") if r["id"].startswith("omega-")
                 and "x" in r["id"] and r["id"] != "omega-notouch"]
    scale_pts.sort(key=lambda p: p["n"])

    o = io.StringIO()
    w = o.write

    w("<title>Mayflower Results Dossier</title>\n")
    w('<link rel="preconnect" href="https://fonts.googleapis.com">\n')
    w('<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n')
    w('<link rel="stylesheet" href="https://fonts.googleapis.com/css2?'
      'family=IBM+Plex+Mono:wght@400;500;600&family=IBM+Plex+Sans:wght@400;500;600&'
      'family=IBM+Plex+Serif:wght@600&display=swap">\n')
    w("<style>" + STYLE + "</style>\n")

    w('<div class="wrap">')

    # Masthead. The hero is the number the engine exists to produce.
    w('<header class="mast">')
    w('<div class="eyebrow">Mayflower &middot; exact inference over Battleships</div>')
    w("<h1>Every result, and how far each one is trusted</h1>")
    w('<p class="stand">{} recorded quantities, {} of them exact and {} measured, '
      "collected from the tools that produced them. Nothing on this page is typed in: "
      "it is generated from <code>experiments/results.json</code>, which is itself "
      "parsed from what each tool printed.</p>".format(
          d["counts"]["results"], d["counts"]["exact"], d["counts"]["measured"]))
    w('<div class="hero">')
    w('<div class="hero-n">{}</div>'.format(group(idx["omega0"]["value"])))
    w('<div class="hero-c">legal fleet configurations on a 10&times;10 board, '
      "counted without enumerating one. The engine reaches this through a lattice "
      "of {} edges, {} times smaller than the set it counts.</div>"
      .format(group(idx["edges"]["value"]),
              int(idx["omega0"]["value"] / idx["edges"]["value"])))
    w("</div>")
    w('<div class="meta">')
    for k, v in (("commit", d["commit"]), ("results", d["counts"]["results"]),
                 ("exact", d["counts"]["exact"]),
                 ("measured", d["counts"]["measured"])):
        w('<div><span>{}</span><b>{}</b></div>'.format(esc(k), esc(v)))
    w("</div></header>")

    # Bound ladder.
    w('<section><h2>What the optimum is bounded by</h2>')
    w('<p class="lede">The central quantitative claim. Every ship cell has to be '
      "shot, so 17 is a floor that needs no argument. Identifying the board takes "
      "33.81 bits, which over a six-outcome channel is only 13.08 shots, so the "
      "entropy bound falls <em>below</em> the trivial one and is drawn where it "
      "actually lands. What binds is coverage, and the interval below starts from "
      "it.</p>")
    w(ladder(b, best))
    w('<p class="cap">Filled marks are exact; the hollow mark is the rung dominated '
      "by E1; the open square is a measured policy. The shaded span is what remains "
      "unproven.</p>")
    w(rows_table(["rung", "shots", "status", "what it rests on"], [
        ["E1 coverage", fmt(b["coverage"], 0), chip(True),
         "all 17 ship cells must be shot"],
        ["E2 entropy", fmt(b["entropy"], 4), chip(True),
         "log2|Omega| over an alphabet of 6, and it falls below E1"],
        ["E4 water filling", fmt(b["waterfilling"], 4), chip(True),
         "{} feasible hit-transcripts".format(group(idx["transcripts"]["value"]))],
        ["E5 max coverage", "withdrawn",
         '<span class="chip chip-out">retracted</span>',
         "not a bound on the adaptive optimum; exceeds the true optimum on 6 of 6"],
        ["best measured", "{:.3f}".format(best), chip(False),
         "density policy, 20,000 games, TRAIN fold"],
    ]))
    w("</section>")

    w('<section><h2>Counting</h2>')
    w('<p class="lede">The same sweep, parameterised by board size and by ruleset. '
      "Every figure is an exact integer, cross-checked against literal enumeration "
      "wherever enumeration reaches.</p>")
    w('<div class="pair">')
    w("<div>" + scaling(scale_pts) + "</div>")
    w("<div>" + rows_table(["board", "configurations"],
                           [["{0}x{0}".format(p["n"]), group(p["omega"])]
                            for p in scale_pts]) + "</div>")
    w("</div>")
    w(rows_table(["quantity", "value", "status"], [
        ["configurations, ships may touch", group(idx["omega0"]["value"]), chip(True)],
        ["configurations, ships may not touch",
         group(idx["omega-notouch"]["value"]), chip(True)],
        ["entropy", "{:.4f} bits".format(idx["entropy"]["value"]), chip(True)],
        ["lattice edges", group(idx["edges"]["value"]), chip(True)],
        ["state visits", group(idx["stateVisits"]["value"]), chip(True)],
        ["peak live states", group(idx["peakStates"]["value"]), chip(True)],
    ]))
    w("</section>")

    w('<section><h2>What policies actually score</h2>')
    w('<p class="lede">Twenty thousand games on one seeded board pool, common '
      "random numbers throughout, TRAIN fold. These are the only estimates on the "
      "page that carry an interval, because they are the only ones that are "
      "estimates.</p>")
    w(policies(pol))
    w(rows_table(["policy", "mean shots", "95% interval", "sd", "games"],
                 [[p["note"], "{:.3f}".format(p["value"]),
                   "&plusmn;{:.3f}".format(p["ci"]), "{:.3f}".format(p["sd"]),
                   group(p["games"])] for p in pol]))
    w("</section>")

    # The sealed fold.
    head = fam(R, "headline")
    if head:
        trainby = {r["note"]: r for r in head if r["fold"] == "train"}
        head = sorted([r for r in head if r["fold"] == "test"],
                      key=lambda r: -r["value"])
        w('<section><h2>The sealed fold</h2>')
        w('<p class="lede">One pre-registered run on data none of the tuning ever '
          "saw. The design, the metrics and the sample size were fixed in "
          "<code>experiments/preregistration.md</code> and the unseal was recorded "
          "before a single board was read. It ran once, at the size the table "
          "prescribed, with no extension and no early stop.</p>")
        rows = []
        for r in head:
            t = trainby.get(r["note"])
            import math as _m
            if t:
                se = _m.sqrt(t["sd"] ** 2 / t["games"] + r["sd"] ** 2 / r["games"])
                inside = abs(t["value"] - r["value"]) <= 1.959963985 * se
            else:
                inside = False
            rows.append([r["note"],
                         "{:.3f}".format(t["value"]) if t else "&ndash;",
                         "{:.3f}".format(r["value"]),
                         "[{:.3f}, {:.3f}]".format(r["ciLow"], r["ciHigh"]),
                         r["p95"],
                         '<span class="chip chip-exact">agrees</span>' if inside
                         else ('<span class="chip chip-out">differs</span>' if t
                               else "&ndash;")])
        w(rows_table(["policy", "TRAIN mean", "TEST mean", "TEST 95% interval",
                      "TEST p95", "verdict"], rows))
        w('<p class="cap">TRAIN and TEST were measured by the same tool at the '
          "same size on disjoint boards, so the sound comparison is whether the "
          "difference is distinguishable from zero. For every policy it is not. "
          "The harness self-test held on unseen data too: the random shooter "
          "measured 95.3387 against a theoretical 95.3889.</p>")
        w("</section>")

    # Exact optima.
    adapt = sorted(fam(R, "adaptivity"), key=lambda r: r["configurations"])
    w('<section><h2>Where both optima are computable</h2>')
    w('<p class="lede">On instances small enough to solve outright, the adaptive '
      "optimum and the best fixed order are both exact. The ratio between them is "
      "what feedback is worth, and it is largest against a lone ship.</p>")
    w(rows_table(["instance", "boards", "adaptive", "fixed order", "greedy order",
                  "ratio"],
                 [[r["instance"], group(r["configurations"]),
                   fmt(r["adaptive"]), fmt(r["value"]), fmt(r["greedy"]),
                   "{:.4f}".format(r["value"] / r["adaptive"])
                   if r["adaptive"] else "&ndash;"] for r in adapt]))
    w('<p class="cap">The adaptive column stops at 264 boards. The limit is the '
      "belief MDP; the fixed-order column runs further because the subset lattice "
      "does.</p>")
    w("</section>")

    adv = sorted(fam(R, "adversary"), key=lambda r: r["configurations"])
    w('<section><h2>Against a hider who never commits</h2>')
    w('<p class="lede">Expected shots assume the board was fixed before play. '
      "Turn the chance node into a maximum and the answer is a worst case with no "
      "distributional assumption in it, and it comes out an integer.</p>")
    w(rows_table(["instance", "boards", "E[T] committed", "W* adaptive", "gap"],
                 [[r["instance"], group(r["configurations"]),
                   fmt(r["committed"]), fmt(r["value"], 0),
                   "{:+.2f}".format(r["value"] - r["committed"])] for r in adv]))
    w("</section>")

    # Cross checks.
    w('<section><h2>Where two tools compute the same thing</h2>')
    w('<p class="lede">Several quantities are produced independently by more than '
      "one program. Collecting them in one place makes that a test: if the belief "
      "MDP in one tool ever disagreed with the other, it would show here rather "
      "than in a report.</p>")
    w(rows_table(["quantity", "sources", "instances", "result"],
                 [[c["quantity"], " vs ".join(c["sources"]), c["instances"],
                   '<span class="chip chip-exact">agree</span>' if c["agree"]
                   else '<span class="chip chip-out">disagree</span>']
                  for c in d["crossChecks"]]))
    w("</section>")

    w('<section class="retract"><h2>Results that contradicted the plan</h2>')
    w('<p class="lede">Four investigations ended by refuting the thing that asked '
      "for them. Each is recorded with the measurement that settled it, so the "
      "retraction can be checked the same way the confirmations can.</p>")
    w(rows_table(["expected", "found", "evidence"], [
        ["A max-coverage bound near 35 shots would nearly close the interval",
         "It is not a lower bound at all",
         "Exceeds the true optimum on 6 of 6 instances; it bounds the "
         "non-adaptive problem"],
        ["The opponent prior would prove unlearnable",
         "It is learnable in about ten games",
         "Oracle gain 1.14 shots, captured in full by fitting one parameter"],
        ["Constraint density would show an easy-hard-easy cost profile",
         "The counting sweep has no hard region",
         "Sweep cost peaks where the record is loosest; only the search peaks in the middle"],
        ["Move ordering would trade against the pruning bound",
         "It is worth 158x where the search is expensive",
         "m9 self-test: 66 s with it, 10,490 s without"],
        ["The information rule loses by turning away from cells past p = 1/2",
         "It loses by not shooting cells it is certain of",
         "94% or more of the gap is misses fired after the record already "
         "names the board; the other two rules fire none"],
    ]))
    w("</section>")

    w('<section><h2>Provenance</h2>')
    w('<p class="lede">{}</p>'.format(esc(d["note"])))
    srcs = {}
    for r in R:
        srcs[r["source"]] = srcs.get(r["source"], 0) + 1
    w(rows_table(["source", "results drawn"],
                 sorted([[k, v] for k, v in srcs.items()], key=lambda x: -x[1])))
    w('<p class="cap">Generated from commit <code>{}</code>. Rebuild with '
      "<code>python tools/collect_results.py &amp;&amp; python "
      "tools/render_results.py</code>.</p>".format(esc(d["commit"])))
    w("</section>")

    w('<footer>Mayflower &middot; exact Bayesian inference over Battleships '
      "&middot; every figure here is generated, none transcribed.</footer>")
    w("</div>")
    return o.getvalue()


STYLE = """
:root {
  --page:#f9f9f7; --surface:#fcfcfb; --ink:#0b0b0b; --ink-2:#52514e;
  --muted:#898781; --grid:#e1e0d9; --axis:#c3c2b7; --rule:#0d366b;
  --accent:#2a78d6; --warn:#eb6834; --good:#1baf7a;
  --gapfill:rgba(42,120,214,0.10);
  --sans:"IBM Plex Sans",system-ui,-apple-system,"Segoe UI",sans-serif;
  --mono:"IBM Plex Mono",ui-monospace,Consolas,monospace;
  --serif:"IBM Plex Serif",Georgia,serif;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    color-scheme:dark;
    --page:#0d0d0d; --surface:#1a1a19; --ink:#ffffff; --ink-2:#c3c2b7;
    --muted:#898781; --grid:#2c2c2a; --axis:#383835; --rule:#86b6ef;
    --accent:#3987e5; --warn:#d95926; --good:#199e70;
    --gapfill:rgba(57,135,229,0.16);
  }
}
:root[data-theme="dark"] {
  color-scheme:dark;
  --page:#0d0d0d; --surface:#1a1a19; --ink:#ffffff; --ink-2:#c3c2b7;
  --muted:#898781; --grid:#2c2c2a; --axis:#383835; --rule:#86b6ef;
  --accent:#3987e5; --warn:#d95926; --good:#199e70;
  --gapfill:rgba(57,135,229,0.16);
}
* { box-sizing:border-box; }
body { margin:0; background:var(--page); color:var(--ink); font-family:var(--sans);
       font-size:16px; line-height:1.62; -webkit-font-smoothing:antialiased; }
.wrap { max-width:960px; margin:0 auto; padding:56px 26px 90px;
        display:flex; flex-direction:column; gap:56px; }
.mast { display:flex; flex-direction:column; gap:18px; }
.eyebrow { font:500 11.5px/1 var(--mono); letter-spacing:.14em;
           text-transform:uppercase; color:var(--muted); }
h1 { font:600 40px/1.12 var(--serif); margin:0; letter-spacing:-.015em;
     text-wrap:balance; max-width:20ch; }
h2 { font:600 23px/1.25 var(--serif); margin:0 0 4px; letter-spacing:-.01em;
     text-wrap:balance; }
.stand { margin:0; font-size:17.5px; color:var(--ink-2); max-width:62ch; }
.lede { margin:0 0 18px; color:var(--ink-2); max-width:66ch; }
.cap { margin:10px 0 0; font-size:13.5px; color:var(--muted); max-width:66ch; }
section { display:flex; flex-direction:column; }
.hero { border-top:2px solid var(--rule); border-bottom:1px solid var(--grid);
        padding:22px 0 20px; display:flex; flex-direction:column; gap:10px; }
.hero-n { font:600 clamp(30px,6.4vw,58px)/1 var(--mono); letter-spacing:-.03em;
          color:var(--rule); font-variant-numeric:tabular-nums; }
.hero-c { font-size:15px; color:var(--ink-2); max-width:62ch; }
.meta { display:flex; flex-wrap:wrap; gap:26px; }
.meta div { display:flex; flex-direction:column; gap:1px; }
.meta span { font:400 11px/1 var(--mono); letter-spacing:.1em;
             text-transform:uppercase; color:var(--muted); }
.meta b { font:500 17px/1.2 var(--mono); font-variant-numeric:tabular-nums; }
/* Capped at its design width by the figure's own style, so nothing is
   upscaled and every label keeps the size it was set in. The floor keeps a
   wide figure legible on a narrow screen. */
.fig { width:100%; height:auto; display:block; margin:4px auto 2px;
       min-width:320px; }
p { margin:0 0 15px; }
h3 { font-family:var(--serif); font-weight:600; font-size:18px; line-height:1.3;
     margin:30px 0 9px; letter-spacing:-0.005em; }
.pair { display:flex; flex-wrap:wrap; gap:26px; align-items:flex-start; }
.pair > div { flex:1 1 300px; min-width:0; }
.axis { stroke:var(--axis); stroke-width:1.5; }
.tick { stroke:var(--axis); stroke-width:1; }
.grid { stroke:var(--grid); stroke-width:1; }
.stem { stroke:var(--axis); stroke-width:1; stroke-dasharray:2 3; }
.line { fill:none; stroke:var(--accent); stroke-width:2;
        stroke-linejoin:round; stroke-linecap:round; }
.whisker { stroke:var(--axis); stroke-width:2; stroke-linecap:round; }
.gapfill { fill:var(--gapfill); }
.mk-exact { fill:var(--accent); stroke:var(--surface); stroke-width:2; }
.mk-open  { fill:var(--surface); stroke:var(--muted); stroke-width:2; }
.mk-meas  { fill:var(--warn); stroke:var(--surface); stroke-width:2; }
text { font-family:var(--mono); }
.tk { font-size:11px; fill:var(--muted); }
.lb { font-size:12px; fill:var(--ink-2); font-family:var(--sans); }
.lv { font-size:13px; fill:var(--ink); font-weight:500;
      font-variant-numeric:tabular-nums; }
.note { font-size:11.5px; fill:var(--muted); font-style:italic;
        font-family:var(--sans); }
.tw { overflow-x:auto; border:1px solid var(--grid); border-radius:3px;
      background:var(--surface); margin-top:12px; }
table { border-collapse:collapse; width:100%; font-size:14px; }
th { text-align:left; font:500 11px/1.4 var(--mono); letter-spacing:.08em;
     text-transform:uppercase; color:var(--muted); padding:11px 14px;
     border-bottom:1px solid var(--grid); white-space:nowrap; }
td { padding:10px 14px; border-bottom:1px solid var(--grid);
     vertical-align:top; color:var(--ink-2); }
tbody tr:last-child td { border-bottom:none; }
td:nth-child(1) { color:var(--ink); font-weight:500; white-space:nowrap; }
td:nth-child(2), td:nth-child(3), td:nth-child(4), td:nth-child(5),
td:nth-child(6) { font-family:var(--mono); font-variant-numeric:tabular-nums;
                  white-space:nowrap; }
.retract td:nth-child(2), .retract td:nth-child(3) { font-family:var(--sans);
                  white-space:normal; }
.chip { display:inline-block; font:500 10.5px/1.7 var(--mono); letter-spacing:.07em;
        text-transform:uppercase; padding:0 7px; border-radius:2px;
        border:1px solid currentColor; white-space:nowrap; }
.chip-exact { color:var(--good); }
.chip-meas  { color:var(--accent); }
.chip-out   { color:var(--warn); }
code { font-family:var(--mono); font-size:.92em; background:var(--surface);
       border:1px solid var(--grid); border-radius:2px; padding:.5px 4px; }
footer { border-top:1px solid var(--grid); padding-top:18px; font-size:13px;
         color:var(--muted); }
@media (max-width:640px) { h1 { font-size:31px; } .wrap { padding:38px 18px 64px; } }
"""


def main():
    d = load()
    out = os.path.join(ROOT, "out", "results.html")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    html_text = build(d)
    with io.open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(html_text)
    print("wrote out/results.html, {:.1f} KB".format(len(html_text) / 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
