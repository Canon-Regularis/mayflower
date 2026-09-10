"""Driving web/engine.js from a headless harness, once.

web/engine.js is an ES module and the browser gets it as a plain script, through
a textual rewrite that turns two exact substrings into two others and then
republishes the names on one global. That rewrite was written out five times:
once in tools/render_report.py, which is the page, and four more times inside
the JS harness strings in tests/test_live_js.py and tests/test_live_exact.py.
One of those even carried the comment "inline the engine exactly as
tools/render_report.py does", which is the duplication admitting itself.

It matters more than its size. The rewrite handles exactly two syntactic forms,
so the moment engine.js contains an import, an export list, an export class or
an export default, all five sites produce a SyntaxError inside a classic script.
Four of them are tests and fail loudly; the fifth is the published page, and
nothing in CI executes it. So splitting engine.js is gated on this having one
home, and this is that home.

engine_script() calls the page's own loader, so the tests inline the engine by
running the same code the page runs rather than a copy of it.
"""

from __future__ import annotations

import io
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def engine_script() -> str:
    """web/engine.js as a classic script, exactly as the page inlines it."""
    tools = os.path.join(ROOT, "tools")
    if tools not in sys.path:
        sys.path.insert(0, tools)
    from render_report import load_engine   # noqa: E402
    return load_engine()


def write_engine_script(suffix: str = "_engine.js") -> str:
    """Write the inlined engine to a temp file and return its path.

    The harnesses read it with readFileSync and eval it whole, so what they run
    is what the page runs, byte for byte.
    """
    fd, path = tempfile.mkstemp(suffix=suffix, text=True)
    with io.open(fd, "w", encoding="utf-8") as fh:
        fh.write(engine_script())
    return path


# The glyph vocabulary both board widgets draw.
#
# web/live.js and web/scrubber.js render the same board on one page, and they
# disagreed: the scrubber drew a miss as "o", a hit as "x" and a sunk shot as
# "+". A reader who learned the vocabulary from one read the other wrong. It is
# stated here once, and each widget's test asserts against what it actually
# painted rather than against the other's source text.
GLYPHS = {"miss": ".", "hit": "o", "sunk": "x"}


def painted_glyphs(pairs):
    """class -> glyph, from what a widget painted. `pairs` is "class|glyph" ..."""
    out = {}
    for pair in pairs.split():
        cls, _, glyph = pair.partition("|")
        state = next((k for k in GLYPHS if k in cls), None)
        if state:
            out.setdefault(state, set()).add(glyph)
    return out
