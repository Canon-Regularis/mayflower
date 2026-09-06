"""The live widget's exact regime, which no test ever entered.

`web/live.js` has two regimes. recompute() takes the sampled branch while at
least SWITCH_TO_EXACT boards survive, and every existing probe hands it the whole
200,000-board pool, so the exact branch, the one that calls constrain() and
marginals() and carries the report's claim that the posterior is exact, ran
nowhere. V8 coverage put those lines at zero.

Reaching it needs no shots: a pool smaller than the threshold is spent from the
first render. The exact sweep over an empty history is then the prior, which is
the marginal table the C++ engine derives independently, so the widget can be
held to it cell by cell.

This sits in the slower gate because that unconstrained sweep is the widget's
heaviest case and costs about thirty seconds. The shipped widget never pays it:
exact mode engages only once a game has cut the survivors below four hundred, by
which point the history has cut the lattice down with them.

    python tests/test_live_exact.py
"""

from __future__ import annotations

import io
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NODE = os.environ.get("MF_NODE", "node")
POOL = os.path.join(ROOT, "web", "pool.bin")
SKIP = 77
# Generous, because contention is the only thing that varies here.
TIMEOUT = 1800
failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail and not ok:
        print("      " + detail)
    if not ok:
        failures += 1

EXACT_HARNESS = r"""
const fs = require('fs');
function makeEl(){ return {innerHTML:'',textContent:'',dataset:{},style:{},
  classList:{add(){},remove(){},toggle(){}},children:[],
  appendChild(c){this.children.push(c);},addEventListener(){},setAttribute(){},
  removeAttribute(){},querySelector(){return null;},querySelectorAll(){return [];}}; }
let eng = fs.readFileSync(process.argv[3],'utf8')
  .split('export const ').join('const ').split('export function ').join('function ');
const live = fs.readFileSync(process.argv[4],'utf8');
const full = fs.readFileSync(process.argv[2]);
function build(n) {
  const root = makeEl();
  root.dataset.pool = full.subarray(0, n * 5).toString('base64');
  const nodes = {'.liveboard':makeEl(),'.livestats':makeEl(),
    '[data-act="new"]':makeEl(),'[data-act="step"]':makeEl(),
    '[data-act="play"]':makeEl(),'[data-act="reveal"]':makeEl()};
  root.querySelector = s => nodes[s] || null;
  global.document = { getElementById: id => id==='live'?root:null, createElement: makeEl };
  global.window = {}; Math.random = () => 0.4242;
  eval('(function(){\n'+eng+'\nwindow.MayflowerEngine={makeInstance,count,marginals,constrain,MISS,HIT,SUNK,FREE,EMPTY,OCCUPIED};\n})();');
  eval(live);
  const board = nodes['.liveboard'].innerHTML;
  const stats = nodes['.livestats'].innerHTML;
  const cell = t => {
    const m = board.match(new RegExp('title="' + t + ': ([0-9.]+)%'));
    return m ? Number(m[1]) : null;
  };
  return { exact: /exact sweep/.test(stats),
           a1: cell('A1'), e5: cell('E5'), e1: cell('E1') };
}
console.log(JSON.stringify({ spent: build(300), justOver: build(400) }));
"""


def run_exact_probe():
    """Build the widget on a pool too small to sample from."""
    harness = os.path.join(ROOT, "out", "_live_exact.js")
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)
    io.open(harness, "w", encoding="utf-8", newline="\n").write(EXACT_HARNESS)
    try:
        proc = subprocess.run(
            [NODE, harness, POOL, os.path.join(ROOT, "web", "engine.js"),
             os.path.join(ROOT, "web", "live.js")],
            capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        # These sweeps are CPU-bound and the clock is the only thing about this
        # test that varies: the widget is deterministic, Math.random pinned. A
        # run that outlasts its timeout raises rather than returning, and the
        # first version let that escape, so a loaded machine failed the test with
        # a traceback and no statement of what went wrong. It cost one red suite
        # at 877 s where the same checks pass in 130 s on an idle machine.
        return {"error": "node did not finish inside {} s".format(TIMEOUT)}
    except OSError as exc:
        return {"error": "node could not be run: {}".format(exc)}
    finally:
        if os.path.exists(harness):
            os.remove(harness)
    if proc.returncode != 0:
        return {"error": "node exited {}: {}".format(
            proc.returncode, proc.stderr.strip()[-120:])}
    return json.loads(proc.stdout.strip().splitlines()[-1])




# A game played through the widget, which nothing had done either. The probes
# above build it and read the first render; none of them presses Step, so the
# SUNK branch of consistent(), the one that rejects a board for completing a ship
# at the wrong moment, was never reached, and no test had ever seen the widget
# finish a game.
#
# The listener is captured rather than dispatched: live.js binds step through
# addEventListener, so a stub that keeps the handler can call it directly.
#
# The whole pool is used because it is the cheapest configuration, which is worth
# stating since the opposite looks true. A smaller pool is spent sooner, so more
# shots run the exact sweep, and the early sweeps are the expensive ones: at two
# thousand boards this takes 67 s against 59 s at two hundred thousand.
PLAY_HARNESS = r"""
const fs = require('fs');
function makeEl(){ return {innerHTML:'',textContent:'',dataset:{},style:{},L:{},
  classList:{add(){},remove(){},toggle(){}},children:[],
  appendChild(c){this.children.push(c);},addEventListener(t,fn){this.L[t]=fn;},
  setAttribute(){},removeAttribute(){},querySelector(){return null;},
  querySelectorAll(){return [];}}; }
let eng = fs.readFileSync(process.argv[3],'utf8')
  .split('export const ').join('const ').split('export function ').join('function ');
const live = fs.readFileSync(process.argv[4],'utf8');
const full = fs.readFileSync(process.argv[2]);

const root = makeEl(); root.dataset.pool = full.toString('base64');
const nodes = {'.liveboard':makeEl(),'.livestats':makeEl(),'[data-act="new"]':makeEl(),
  '[data-act="step"]':makeEl(),'[data-act="play"]':makeEl(),'[data-act="reveal"]':makeEl()};
root.querySelector = s => nodes[s] || null;
global.document = { getElementById: id => id==='live'?root:null, createElement: makeEl };
global.window = {}; Math.random = () => 0.4242;
global.setTimeout = () => 0; global.clearTimeout = () => {};
eval('(function(){\n'+eng+'\nwindow.MayflowerEngine={makeInstance,count,marginals,constrain,MISS,HIT,SUNK,FREE,EMPTY,OCCUPIED};\n})();');
eval(live);

const step = nodes['[data-act="step"]'].L.click;
if (typeof step !== 'function') { console.log(JSON.stringify({drivable:false})); process.exit(0); }
let shots = 0, firstExact = -1, done = false, omegas = [], rises = [];
while (shots < 120) {
  step(); shots++;
  const st = nodes['.livestats'].innerHTML;
  const flat = st.replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ');
  if (firstExact < 0 && /exact sweep/.test(st)) firstExact = shots;
  const om = flat.match(/possible ([0-9,]+)/);
  if (om) {
    const v = Number(om[1].split(',').join(''));
    if (omegas.length && v > omegas[omegas.length - 1])
      rises.push(shots + ': ' + omegas[omegas.length - 1] + ' -> ' + v);
    omegas.push(v);
  }
  if (flat.indexOf('17 / 17') >= 0) { done = true; break; }
}
const sunk = (nodes['.liveboard'].innerHTML.match(/sunk/g) || []).length;
console.log(JSON.stringify({drivable:true, shots, done, firstExact, sunk, rises,
                            lastOmega: omegas.length ? omegas[omegas.length-1] : null}));
"""


def run_play_probe():
    """Press Step until the fleet is cleared."""
    harness = os.path.join(ROOT, "out", "_live_play.js")
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)
    io.open(harness, "w", encoding="utf-8", newline="\n").write(PLAY_HARNESS)
    try:
        proc = subprocess.run(
            [NODE, harness, POOL, os.path.join(ROOT, "web", "engine.js"),
             os.path.join(ROOT, "web", "live.js")],
            capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        # These sweeps are CPU-bound and the clock is the only thing about this
        # test that varies: the widget is deterministic, Math.random pinned. A
        # run that outlasts its timeout raises rather than returning, and the
        # first version let that escape, so a loaded machine failed the test with
        # a traceback and no statement of what went wrong. It cost one red suite
        # at 877 s where the same checks pass in 130 s on an idle machine.
        return {"error": "node did not finish inside {} s".format(TIMEOUT)}
    except OSError as exc:
        return {"error": "node could not be run: {}".format(exc)}
    finally:
        if os.path.exists(harness):
            os.remove(harness)
    if proc.returncode != 0:
        return {"error": "node exited {}: {}".format(
            proc.returncode, proc.stderr.strip()[-120:])}
    return json.loads(proc.stdout.strip().splitlines()[-1])



def main():
    print("the live widget's exact regime")
    print("==============================")
    if not os.path.exists(POOL):
        print("  web/pool.bin is missing; run tools/export_pool")
        return SKIP

    ex = run_exact_probe()
    if ex is None or "error" in ex:
        check(False, "the exact-handoff probe runs",
              (ex or {}).get("error", "no output"))
    else:
        spent, over = ex["spent"], ex["justOver"]
        check(spent["exact"], "a pool below the threshold switches to the exact sweep")
        check(not over["exact"], "and one at the threshold is still sampled",
              "400 boards took the exact branch")

        # The exact sweep over an empty history is the prior, which is the table
        # the C++ engine derives: corner 0.0800, centre 0.2136, edge midpoint
        # 0.1667, rendered to one decimal.
        for label, got, want in (("corner A1", spent["a1"], 8.0),
                                 ("centre E5", spent["e5"], 21.4),
                                 ("edge midpoint E1", spent["e1"], 16.7)):
            check(got is not None and abs(got - want) < 0.05,
                  "the exact sweep puts {} at {}%".format(label, want),
                  "got {}".format(got))

        # 400 sampled boards miss that table by a wide margin, so the three
        # checks above are reading the exact sweep and not the renderer.
        drift = max(abs(over[k] - w) for k, w in
                    (("a1", 8.0), ("e5", 21.4), ("e1", 16.7)) if over[k] is not None)
        check(drift > 0.5,
              "and a sampled pool does not land on that table by accident",
              "closest sampled figure was within {:.2f} points".format(drift))

    play = run_play_probe()
    if play is None or "error" in play:
        check(False, "the play-through probe runs",
              (play or {}).get("error", "no output"))
    elif not play.get("drivable"):
        check(False, "the widget binds a step handler that can be pressed")
    else:
        check(play["done"], "the widget plays a game out to all seventeen hits",
              "stopped after {} shots".format(play["shots"]))
        check(17 <= play["shots"] <= 100,
              "in a plausible number of shots",
              "took {}".format(play["shots"]))

        # Counts are monotonically non-increasing: evidence never revives a
        # configuration it ruled out. The readout crosses from the sampled
        # estimate to the exact sweep partway through, and it has to hold across
        # that too.
        check(not play["rises"], "and the hypothesis count never rises",
              "; ".join(play["rises"][:3]))
        check(0 < play["firstExact"] < play["shots"],
              "the game crosses from the sampled regime into the exact one",
              "first exact at shot {} of {}".format(play["firstExact"], play["shots"]))
        check(play["sunk"] > 0,
              "and sinks a ship, which is what exercises the SUNK rejection",
              "no sunk cell on the final board")

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
