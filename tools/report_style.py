"""Values and helpers the report layer shares.

Two renderers and the figure builder each grew their own copy of the pieces
below. Only some of that was real duplication, and the difference matters:

  esc   was byte identical in tools/build_report.py and tools/render_results.py,
        so it has one home here and both import it.
  Z_95  was retyped as the literal 1.959963985 in three places that all mean the
        same thing, the two-sided normal quantile at 95 percent.

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

# The two-sided normal quantile at 95 percent. Written to ten digits because the
# calibration work in python/stats.py compares intervals whose coverage differs
# in the third decimal, and 1.96 is not the same number.
Z_95 = 1.959963985


def esc(s):
    return html.escape(str(s), quote=True)
