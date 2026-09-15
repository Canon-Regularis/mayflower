// How long the browser engine takes to sweep the exact posterior.
//
// tools/render_report.py quotes three of these numbers to explain why the live
// widget has two estimators rather than one. They cannot come out of
// report_data, which times the C++ sweep and not this one, so they are measured
// here and the constants in that file name this script. Re-run it and update
// SWEEP_TURN0, SWEEP_SHOT14 and SWEEP_SUBSECOND together.
//
//     node tools/sweep_timing.mjs
//
// Median of three, because a single timing run on a machine with other work on
// it is not a measurement. This project has already set a default twice off one
// such run and been wrong both times.
//
// Three is the floor here, not a comfortable margin. The empty record measures
// 58.2, 38.6 and 27.4 seconds over three consecutive runs as the JIT warms, a
// spread of 2.1x within one invocation, so the first column is worth quoting as
// an order of magnitude and not as a figure. Later rows settle: shot 14 gives
// 3.26, 3.49, 3.59.
//
// The records below are all misses. A miss is the weakest constraint a shot can
// place, so a real game at the same shot count sweeps faster than this: these
// are upper bounds at their shot count rather than typical cases.

import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const E = await import("file://" + join(here, "..", "web", "engine.js"));

const REPEATS = 3;
const inst = E.makeInstance(10, 10, [5, 4, 3, 3, 2]);

function median(xs) {
  const s = [...xs].sort((a, b) => a - b);
  return s[(s.length - 1) >> 1];
}

function timeSweep(history) {
  const { cells, gate } = E.constrain(inst, history);
  const runs = [];
  for (let i = 0; i < REPEATS; i++) {
    const t0 = process.hrtime.bigint();
    E.marginals(inst, cells, gate);
    runs.push(Number(process.hrtime.bigint() - t0) / 1e9);
  }
  return { median: median(runs), runs };
}

// The parity lattice, which is the order a hunting policy shoots in.
const parity = [];
for (let r = 0; r < 10; r++)
  for (let c = 0; c < 10; c++) if ((r + c) % 2 === 0) parity.push(r * 10 + c);

const missesAt = (t) =>
  parity.slice(0, t).map((cell) => ({ cell, outcome: E.MISS, length: 0 }));

console.log("exact sweep in web/engine.js, median of " + REPEATS);
console.log("shots   median s   runs");
for (const t of [0, 4, 8, 12, 14, 16, 20, 24, 26, 28, 30, 34]) {
  const { median: m, runs } = timeSweep(missesAt(t));
  console.log(
    String(t).padStart(5) + "   " + m.toFixed(2).padStart(8) + "   " +
    runs.map((r) => r.toFixed(2)).join(" "));
}
