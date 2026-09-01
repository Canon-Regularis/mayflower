"""The live widget's playback, driven headlessly.

web/live.js runs the real sweep in the page and nothing ever ran it. Its Play
button chains setTimeout without keeping the handle, so a pending tick survives
a pause. Press Play again before it fires and the old chain sees `playing` true
again and carries on beside the new one, and each toggle inside one period adds
another. Every extra chain is an extra posterior recompute per tick.

The engine is inlined the same way the report inlines it, so this drives the
shipped code rather than a copy.

    python tests/test_live_js.py
"""

from __future__ import annotations

import base64
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NODE = os.environ.get("MF_NODE", "node")
POOL = os.path.join(ROOT, "web", "pool.bin")

SKIP = 77

failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    if not ok:
        failures += 1


# A DOM only as wide as live.js touches, plus a clock the test drives by hand.
HARNESS = r"""
const fs = require('fs');

let now = 0;
const timers = [];
global.setTimeout = (fn, ms) => { const t = {at: now + ms, fn}; timers.push(t); return t; };
global.clearTimeout = (t) => { if (t) t.dead = true; };
function advance(ms) {
  const until = now + ms;
  for (;;) {
    const due = timers.filter(t => !t.dead && !t.done && t.at <= until)
                      .sort((a, b) => a.at - b.at)[0];
    if (!due) break;
    now = due.at; due.done = true; due.fn();
  }
  now = until;
}
function pending() { return timers.filter(t => !t.dead && !t.done).length; }

global.atob = (b64) => Buffer.from(b64, 'base64').toString('binary');

function makeEl(tag) {
  return {
    tagName: tag, children: [], listeners: {}, attrs: {}, dataset: {},
    className: '', textContent: '', innerHTML: '', disabled: false,
    appendChild(c) { this.children.push(c); return c; },
    addEventListener(k, fn) { (this.listeners[k] = this.listeners[k] || []).push(fn); },
    setAttribute(k, v) { this.attrs[k] = v; },
    fire(k, ev) { (this.listeners[k] || []).forEach(fn => fn(ev || {preventDefault(){}})); },
  };
}

const root = makeEl('div');
root.dataset.pool = fs.readFileSync(process.argv[2]).toString('base64');
const nodes = {
  '.liveboard': makeEl('div'),
  '.livestats': makeEl('div'),
  '[data-act="new"]': makeEl('button'),
  '[data-act="step"]': makeEl('button'),
  '[data-act="play"]': makeEl('button'),
  '[data-act="reveal"]': makeEl('button'),
};
root.querySelector = (sel) => nodes[sel] || null;

global.document = { getElementById: (id) => (id === 'live' ? root : null), createElement: makeEl };
global.window = {};

// Inline the engine exactly as tools/render_report.py does.
let eng = fs.readFileSync(process.argv[3], 'utf8');
eng = eng.split('export const ').join('const ').split('export function ').join('function ');
eval('(function(){\n' + eng +
     '\nwindow.MayflowerEngine = { makeInstance, count, marginals, constrain,' +
     ' MISS, HIT, SUNK, FREE, EMPTY, OCCUPIED };\n})();');

// A fixed board, so the run is reproducible.
Math.random = () => 0.4242;

eval(fs.readFileSync(process.argv[4], 'utf8'));

const play = nodes['[data-act="play"]'];
const stats = nodes['.livestats'];
const shots = () => (stats.innerHTML.match(/shot/g) || []).length;

const out = {};

// One chain: a tick every 260 ms.
play.fire('click');
out.pendingAfterPlay = pending();
advance(260); out.a1 = pending();
advance(260); out.a2 = pending();

// Pause, then resume inside the same period. The pending tick from the first
// chain has not fired yet, so it must not become a second chain.
play.fire('click');
out.pendingAfterPause = pending();
play.fire('click');
out.pendingAfterResume = pending();

// Toggle twice more inside one period.
play.fire('click'); play.fire('click');
play.fire('click'); play.fire('click');
out.pendingAfterFourMore = pending();

advance(260);
out.pendingAfterTick = pending();

console.log(JSON.stringify(out));
"""


def main():
    print("the live widget's playback")
    print("==========================")
    if not os.path.exists(POOL):
        print("  web/pool.bin is missing")
        return SKIP

    harness = os.path.join(ROOT, "out", "_live_harness.js")
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)
    io.open(harness, "w", encoding="utf-8", newline="\n").write(HARNESS)
    try:
        proc = subprocess.run(
            [NODE, harness, POOL,
             os.path.join(ROOT, "web", "engine.js"),
             os.path.join(ROOT, "web", "live.js")],
            capture_output=True, text=True)
    except FileNotFoundError:
        print("  node is not on PATH")
        return SKIP
    finally:
        if os.path.exists(harness):
            os.remove(harness)

    if proc.returncode != 0:
        print("  the harness failed:\n" + (proc.stderr or "")[:2000])
        return 1

    import json
    r = json.loads(proc.stdout.strip().splitlines()[-1])

    check(r["pendingAfterPlay"] == 1, "Play schedules exactly one tick",
          "{} pending".format(r["pendingAfterPlay"]))
    check(r["a1"] == 1 and r["a2"] == 1, "one tick stays scheduled while playing",
          "{} then {} pending".format(r["a1"], r["a2"]))
    check(r["pendingAfterPause"] == 0, "Pause cancels the tick it scheduled",
          "{} left pending after Pause".format(r["pendingAfterPause"]))
    check(r["pendingAfterResume"] == 1, "resuming inside one period runs one chain",
          "{} pending after Play, Pause, Play".format(r["pendingAfterResume"]))
    check(r["pendingAfterFourMore"] == 1, "four more toggles still leave one chain",
          "{} pending".format(r["pendingAfterFourMore"]))
    check(r["pendingAfterTick"] == 1, "a tick replaces itself rather than multiplying",
          "{} pending after the next tick".format(r["pendingAfterTick"]))

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
