"""The belief scrubber's playback, driven headlessly.

web/scrubber.js is shipped inside the report and nothing ran it. Its Play button
was disabled whenever the system asked for reduced motion, which on a common
Windows default is every reader, so the control was inert and looked broken.

This drives the real file against a minimal DOM and a fake clock: press Play,
advance time, and check the frame index moves the way the controls say it will.
The controls are the point of the test, since a step that could go negative would
run the game backwards past its own start and a period of zero would spin.

    python tests/test_scrubber_js.py
"""

from __future__ import annotations

import io
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NODE = os.environ.get("MF_NODE", "node")
FIGURES = os.path.join(ROOT, "out", "figures.json")

SKIP = 77

failures = 0


def check(ok, what, detail=""):
    global failures
    print("  {:<58} {}".format(what, "ok" if ok else "FAILED"))
    if detail:
        print("      " + detail)
    if not ok:
        failures += 1


# A DOM only as wide as scrubber.js actually touches, plus a clock the test
# drives by hand so a three-second period costs no wall time.
HARNESS = r"""
const fs = require('fs');

let now = 0;
const timers = [];
global.setTimeout = (fn, ms) => { const t = {at: now + ms, fn, id: timers.length}; timers.push(t); return t; };
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

function makeEl(tag) {
  return {
    tagName: tag, children: [], listeners: {}, style: {}, dataset: {}, attrs: {},
    className: '', textContent: '', innerHTML: '', value: '', type: '', min: '', max: '',
    step: '', title: '', disabled: false, tabIndex: 0, inputMode: '',
    appendChild(c) { this.children.push(c); return c; },
    addEventListener(k, fn) { (this.listeners[k] = this.listeners[k] || []).push(fn); },
    setAttribute(k, v) { this.attrs[k] = v; },
    getAttribute(k) { return this.attrs[k]; },
    fire(k, ev) { (this.listeners[k] || []).forEach(fn => fn(ev || {preventDefault(){}})); },
  };
}

const payload = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const root = makeEl('div');
root.dataset.frames = JSON.stringify(payload);

global.document = {
  getElementById: (id) => (id === 'scrub' ? root : null),
  createElement: makeEl,
};
global.window = { matchMedia: (q) => ({matches: q.indexOf('reduced-motion') >= 0}) };

eval(fs.readFileSync(process.argv[3], 'utf8'));

// scrubber.js builds: board, controls(play, slider, step, speed, reveal), readout, mirror
const controls = root.children[1];
const play = controls.children[0];
const slider = controls.children[1];
const stepIn = controls.children[2].children[1];
const speedIn = controls.children[3].children[1];
const turnOf = () => parseInt(slider.value, 10);

const out = {};
out.playEnabled = !play.disabled;
out.stepDefault = stepIn.value;
out.speedDefault = speedIn.value;
out.stepMin = stepIn.min;
out.startTurn = turnOf();

// Press play, then advance exactly three seconds at a time.
play.fire('click');
out.labelWhilePlaying = play.textContent;
advance(3000); out.after3s = turnOf();
advance(3000); out.after6s = turnOf();
advance(3000); out.after9s = turnOf();

// A bigger step takes effect on the next tick, with no restart.
stepIn.value = '5'; stepIn.fire('change');
advance(3000); out.afterStep5 = turnOf();

// A shorter period reschedules the pending tick rather than waiting it out.
speedIn.value = '0.5'; speedIn.fire('change');
advance(500); out.afterFast = turnOf();

// Negative is clamped to zero and written back, and the frame then holds.
stepIn.value = '-4'; stepIn.fire('change');
out.clampedStep = stepIn.value;
advance(500); out.afterNegative = turnOf();
advance(500); out.afterNegativeAgain = turnOf();

// Pause stops the clock advancing the frame.
play.fire('click');
out.labelAfterPause = play.textContent;
const held = turnOf();
advance(60000); out.afterPause = turnOf(); out.heldAt = held;

// Play from the end restarts at the prior.
stepIn.value = '1'; stepIn.fire('change');
speedIn.value = '0.25'; speedIn.fire('change');
play.fire('click');
advance(600000);
out.reachedEnd = turnOf();
out.labelAtEnd = play.textContent;
play.fire('click');
out.restarted = turnOf();

console.log(JSON.stringify(out));
"""


def main():
    print("the belief scrubber's playback")
    print("==============================")
    if not os.path.exists(FIGURES):
        print("  out/figures.json is missing; run tools/report_data first")
        return SKIP

    fig = json.load(io.open(FIGURES, encoding="utf-8"))
    game = next((g for g in fig["collapse"] if "frames" in g), None)
    if game is None:
        check(False, "a recorded game with belief frames")
        return 1

    payload = {
        "width": fig["prior"]["width"], "height": fig["prior"]["height"],
        "omega": game["omega"], "frames": game["frames"], "cells": game["cells"],
        "outcomes": game["outcomes"], "truth": game["truth"],
    }
    turns = len(game["omega"])

    harness = os.path.join(ROOT, "_scrubber_probe.cjs")
    data = os.path.join(ROOT, "_scrubber_frames.json")
    io.open(harness, "w", encoding="utf-8", newline="\n").write(HARNESS)
    io.open(data, "w", encoding="utf-8", newline="\n").write(json.dumps(payload))
    try:
        proc = subprocess.run(
            [NODE, harness, data, os.path.join(ROOT, "web", "scrubber.js")],
            cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            print("  the harness failed:", proc.stderr[:700])
            return 1
        r = json.loads(proc.stdout.strip().split("\n")[-1])
    finally:
        for f in (harness, data):
            if os.path.exists(f):
                os.remove(f)

    check(r["playEnabled"],
          "Play is live even when the system asks for reduced motion")
    check(r["stepDefault"] == "1" and r["stepMin"] == "0",
          "the step starts at 1 and cannot go below 0",
          "step={}, min={}".format(r["stepDefault"], r["stepMin"]))
    check(r["speedDefault"] == "3", "the period starts at 3 seconds",
          "every {}s".format(r["speedDefault"]))
    check(r["labelWhilePlaying"] == "Pause", "pressing Play offers Pause")
    check((r["after3s"], r["after6s"], r["after9s"]) == (1, 2, 3),
          "one turn every three seconds",
          "turns {} {} {} after 3s, 6s, 9s".format(r["after3s"], r["after6s"], r["after9s"]))
    check(r["afterStep5"] == r["after9s"] + 5,
          "a larger step applies on the next tick, with no restart",
          "{} to {}".format(r["after9s"], r["afterStep5"]))
    check(r["afterFast"] == r["afterStep5"] + 5,
          "a shorter period reschedules rather than waiting out the old one",
          "{} to {} in half a second".format(r["afterStep5"], r["afterFast"]))
    check(r["clampedStep"] == "0" and r["afterNegativeAgain"] == r["afterNegative"],
          "a negative step is clamped to 0 and the frame holds",
          "the box reads {}".format(r["clampedStep"]))
    check(r["labelAfterPause"] == "Play" and r["afterPause"] == r["heldAt"],
          "Pause stops the clock moving the frame")
    check(r["reachedEnd"] == turns - 1 and r["labelAtEnd"] == "Play",
          "playback stops itself at the last turn",
          "ended at {} of {}".format(r["reachedEnd"], turns - 1))
    check(r["restarted"] == 0, "Play from the end restarts at the prior")

    print("\n" + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
