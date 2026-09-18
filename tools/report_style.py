"""Values and helpers the report layer shares.

Two renderers and the figure builder each grew their own copy of the pieces
below. Only some of that was real duplication, and the difference matters:

  esc   was byte identical in tools/build_report.py and tools/render_results.py,
        so it has one home here and both import it.
  Z_95  was retyped as the literal 1.959963985 in three places that all mean the
        same thing, the two-sided normal quantile at 95 percent. Three of the
        seven, as it turned out: the sentence above used to claim the retyping
        was fixed, and what was fixed was the three renderer sites here. The
        other four wrote the shorter 1.959964, one of them in C++, and both
        spellings reached the same rendered page. They now agree, on a value
        that is neither: see kZ95 in include/mayflower/constants.hpp.

What is deliberately not here is fmt and svg_open. They carry the same names in
build_report.py and render_results.py and they are not the same functions:
fmt(n, decimals=0) groups an integer with thousands separators, while
fmt(v, places=4) renders a float and returns an en dash for None; svg_open takes
a title in one and not in the other. Merging them would change what a page
renders, and the contract for this refactor is that no page changes. They are
recorded here as a naming collision across two modules rather than repaired.
"""

from __future__ import annotations

import html

# The two-sided normal quantile at 95 percent, to full double precision. The
# calibration work in python/stats.py compares intervals whose coverage differs
# in the third decimal, so 1.96 is far too coarse, and the 1.959963985 written
# here before was a nine-digit rounding that disagreed with the C++ tools'
# 1.959964 at the seventh. Pinned against kZ95 and python/stats.Z_95 by
# tests/test_stated_counts.py, the way the fold vector is pinned across the
# same boundary.
Z_95 = 1.959963984540054


# The palette, once.
#
# out/report.html and out/results.html are two pages of one design system and
# each carried a full copy of the hexes, under different names: --series-1,
# --series-2 and --series-3 in the report against --accent, --warn and --good
# in the dossier. The values agreed, which is what made the copies invisible,
# and the one value that had drifted apart was --gapfill.
#
# The two naming vocabularies stay. They mean different things: the report
# distinguishes series by position in a legend, the dossier by role in a
# sentence. What they share is the ink, and that is what lives here.
LIGHT = {
    "page":        "#f9f9f7",
    "surface":     "#fcfcfb",
    "ink":         "#0b0b0b",
    "ink-2":       "#52514e",
    "muted":       "#898781",
    "grid":        "#e1e0d9",
    "axis":        "#c3c2b7",
    "rule":        "#0d366b",
    "series-1":    "#2a78d6",
    "series-2":    "#eb6834",
    "series-3":    "#1baf7a",
}

DARK = {
    "page":        "#0d0d0d",
    "surface":     "#1a1a19",
    "ink":         "#ffffff",
    "ink-2":       "#c3c2b7",
    "muted":       "#898781",
    "grid":        "#2c2c2a",
    "axis":        "#383835",
    "rule":        "#86b6ef",
    "series-1":    "#3987e5",
    "series-2":    "#d95926",
    "series-3":    "#199e70",
}

# The two typefaces both pages set. render_report.py used --sans, --mono and
# --panel without defining any of them for a while, which dropped the scrubber
# controls' whole font declaration; they are stated here so that cannot recur
# in one page and not the other.
SANS = '"IBM Plex Sans",system-ui,-apple-system,"Segoe UI",sans-serif'
MONO = '"IBM Plex Mono",ui-monospace,Consolas,monospace'
SERIF = '"IBM Plex Serif",Georgia,serif'


def esc(s):
    return html.escape(str(s), quote=True)
