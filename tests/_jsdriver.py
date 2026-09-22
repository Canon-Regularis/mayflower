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
import json
import os
import subprocess
import sys
import tempfile
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _harness import ROOT  # noqa: E402

# Distributions disagree about whether the binary is node or nodejs, so the
# build hands over the one it found. One home: the four Node-driven tests all
# import from here and all four carried this line, with this comment, while
# none of them imported it.
NODE = os.environ.get("MF_NODE", "node")


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
GLYPHS: dict[str, str] = {"miss": ".", "hit": "o", "sunk": "x"}


def painted_glyphs(pairs: str) -> dict[str, set[str]]:
    """class -> glyph, from what a widget painted. `pairs` is "class|glyph" ..."""
    out: dict[str, set[str]] = {}
    for pair in pairs.split():
        cls, _, glyph = pair.partition("|")
        state = next((k for k in GLYPHS if k in cls), None)
        if state:
            out.setdefault(state, set()).add(glyph)
    return out


# Answering a batch of questions with the real engine, as an ES module rather
# than through the inlining rewrite above. This lived in
# tests/test_engine_js.py, and the live widget's handoff check needs it too:
# that check compares the widget's sampled posterior against the exact one,
# so it needs both the pool and the engine.

DRIVER = r"""
import { makeInstance, count, constrain, marginals }
  from './web/engine.js';

const jobs = JSON.parse(process.argv[2]);
const out = [];
for (const j of jobs) {
  const inst = makeInstance(j.w, j.h, j.fleet);
  if (j.kind === 'cells') {
    out.push(Number(count(inst, Int8Array.from(j.cells), null)));
  } else {
    const hist = j.history.map(s => ({ cell: s[0], outcome: s[1], length: s[2] }));
    const { cells, gate } = constrain(inst, hist);
    if (j.kind === 'marginals') {
      const m = marginals(inst, cells, gate);
      const t = Number(m.total);
      out.push(Array.from(m.occ, v => (t > 0 ? Number(v) / t : 0)));
    } else {
      out.push(Number(count(inst, cells, gate)));
    }
  }
}
console.log(JSON.stringify(out));
"""


def run_js(jobs: list[dict[str, Any]]) -> Any:
    driver = os.path.join(ROOT, "_engine_probe.mjs")
    io.open(driver, "w", encoding="utf-8", newline="\n").write(DRIVER)
    try:
        proc = subprocess.run([NODE, driver, json.dumps(jobs)],
                              cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            print("node failed:", proc.stderr[:600])
            return None
        return json.loads(proc.stdout.strip().split("\n")[-1])
    finally:
        if os.path.exists(driver):
            os.remove(driver)


# Driving a widget harness under node and reading back its one JSON line.
#
# tests/test_live_exact.py wrote this out twice, byte for byte across
# twenty-three lines including the thirteen-line comment below, differing only
# in the temporary file's name and which harness string it wrote there.
# tests/test_live_js.py has a third, shorter copy.
#
# Every failure comes back as {"error": ...} rather than raising. The first
# version let TimeoutExpired escape, so a loaded machine failed the test with a
# traceback and no statement of what went wrong: one red suite at 877 s where
# the same checks pass in 130 s idle. These sweeps are CPU-bound and the clock
# is the only thing about them that varies, since the widget is deterministic
# and Math.random is pinned, so a slow machine is not a failing one.
def run_widget_probe(name: str, source: str, pool: str, engine_script: str,
                     timeout: float, env: dict[str, str]) -> Any:
    """Run `source` as a node harness over the pool, engine and live.js."""
    out_dir = os.path.join(ROOT, "out")
    os.makedirs(out_dir, exist_ok=True)
    harness = os.path.join(out_dir, name)
    io.open(harness, "w", encoding="utf-8", newline="\n").write(source)
    try:
        proc = subprocess.run(
            [NODE, harness, pool, engine_script, os.path.join(ROOT, "web", "live.js")],
            capture_output=True, text=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return {"error": "node did not finish inside {} s".format(timeout)}
    except OSError as exc:
        return {"error": "node could not be run: {}".format(exc)}
    finally:
        if os.path.exists(harness):
            os.remove(harness)
    if proc.returncode != 0:
        return {"error": "node exited {}: {}".format(
            proc.returncode, proc.stderr.strip()[-120:])}
    return json.loads(proc.stdout.strip().splitlines()[-1])
