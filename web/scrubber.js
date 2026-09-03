// The belief scrubber.
//
// One recorded game, replayed a shot at a time, with the exact posterior at
// every turn. The frames are precomputed and quantised to a byte per cell, so
// nothing is recounted in the browser: scrubbing is a lookup and stays smooth
// under the keyboard.
//
// The board is the same widget used everywhere else on the page, with the same
// glyph vocabulary: open ring for a miss, filled disc for a hit, cross for the
// shot that sank a ship, and a solid green outline for the hidden truth once it is
// revealed.

(function () {
  const root = document.getElementById("scrub");
  if (!root) return;

  const RAMP = ["#cde2fb", "#b7d3f6", "#9ec5f4", "#86b6ef", "#6da7ec", "#5598e7",
                "#3987e5", "#2a78d6", "#256abf", "#1c5cab", "#184f95", "#104281",
                "#0d366b"];

  const data = JSON.parse(root.dataset.frames);
  const W = data.width, H = data.height, CELLS = W * H;
  const turns = data.omega.length;               // frame 0 is the prior
  const frames = data.frames;                     // turns * CELLS bytes
  const cells = data.cells, outcomes = data.outcomes, truth = data.truth;

  // The payload has to describe one game, and every part of it is indexed
  // against a length taken from another part. Without this the widget drew
  // whatever it could: a width of 7 against 10-wide frame data painted a
  // 70-cell board where every cell showed another cell's posterior, and empty
  // frames painted a full board out of undefined. A blank widget with a reason
  // beats a confident picture of nothing.
  const problems = [];
  if (!(W > 0 && H > 0)) problems.push(`board is ${W}x${H}`);
  if (turns < 1) problems.push("no turns recorded");
  if (frames.length !== turns * CELLS)
    problems.push(`${frames.length} frame bytes against ${turns} turns x ${CELLS} cells`);
  if (cells.length !== turns - 1)
    problems.push(`${cells.length} shots against ${turns - 1} turns after the prior`);
  if (outcomes.length !== cells.length)
    problems.push(`${outcomes.length} outcomes against ${cells.length} shots`);
  if (truth.length !== CELLS)
    problems.push(`${truth.length} truth cells against ${CELLS}`);
  if (problems.length) {
    root.textContent = "The recorded game does not describe this board: " +
                       problems.join("; ") + ".";
    return;
  }

  let turn = 0;
  let revealed = false;
  let playing = null;

  // ---- structure ----------------------------------------------------------
  const board = document.createElement("div");
  board.className = "liveboard scrubboard";
  board.setAttribute("role", "grid");
  board.setAttribute("aria-label", "Posterior probability of each cell holding a ship");

  const cellEls = [];
  for (let i = 0; i < CELLS; i++) {
    const el = document.createElement("div");
    el.className = "lc";
    el.setAttribute("role", "gridcell");
    board.appendChild(el);
    cellEls.push(el);
  }

  const controls = document.createElement("div");
  controls.className = "scrubctl";

  const play = document.createElement("button");
  play.type = "button";
  play.className = "scrubbtn";
  play.textContent = "Play";

  const slider = document.createElement("input");
  slider.type = "range";
  slider.min = "0";
  slider.max = String(turns - 1);
  slider.value = "0";
  slider.step = "1";
  slider.className = "scrubrange";
  slider.setAttribute("aria-label", "Turn");

  // A labelled number box. Returning the input separately keeps the reads below
  // free of DOM traversal.
  function numberControl(labelText, value, min, step, suffix) {
    const wrap = document.createElement("label");
    wrap.className = "scrubnum";
    const name = document.createElement("span");
    name.textContent = labelText;
    const input = document.createElement("input");
    input.type = "number";
    input.min = String(min);
    input.step = String(step);
    input.value = String(value);
    input.inputMode = "decimal";
    wrap.appendChild(name);
    wrap.appendChild(input);
    if (suffix) {
      const unit = document.createElement("span");
      unit.textContent = suffix;
      wrap.appendChild(unit);
    }
    return { wrap: wrap, input: input };
  }

  const stepCtl = numberControl("step", 1, 0, 1, "");
  const speedCtl = numberControl("every", 3, 0.25, 0.25, "s");

  const reveal = document.createElement("button");
  reveal.type = "button";
  reveal.className = "scrubbtn";
  reveal.textContent = "Reveal fleet";

  controls.appendChild(play);
  controls.appendChild(slider);
  controls.appendChild(stepCtl.wrap);
  controls.appendChild(speedCtl.wrap);
  controls.appendChild(reveal);

  const readout = document.createElement("div");
  readout.className = "scrubread";

  // A table mirror, so the frame is readable without seeing the colours.
  const mirror = document.createElement("table");
  mirror.className = "visually-hidden";
  mirror.innerHTML = "<caption>Posterior by cell at the selected turn</caption>";
  const mirrorBody = document.createElement("tbody");
  mirror.appendChild(mirrorBody);

  root.appendChild(board);
  root.appendChild(controls);
  root.appendChild(readout);
  root.appendChild(mirror);

  // ---- drawing ------------------------------------------------------------
  function colourFor(p) {
    // Fixed limits across every turn, so frames are comparable to each other.
    // A per-frame rescale would make the collapse invisible, which is the one
    // thing this figure exists to show.
    const idx = Math.min(RAMP.length - 1, Math.max(0, Math.round(p * (RAMP.length - 1) * 2.2)));
    return RAMP[idx];
  }

  const shotAt = new Array(CELLS).fill(-1);

  function draw() {
    shotAt.fill(-1);
    for (let t = 0; t < turn; t++) shotAt[cells[t]] = outcomes[t];

    const base = turn * CELLS;
    let rows = "";
    for (let r = 0; r < H; r++) {
      let cellsHtml = "";
      for (let c = 0; c < W; c++) {
        const i = r * W + c;
        const p = frames[base + i] / 255;
        const el = cellEls[i];
        const o = shotAt[i];
        el.className = "lc" + (o === 0 ? " miss" : o === 1 ? " hit" : o === 2 ? " sunk" : "");
        el.textContent = o === 0 ? "o" : o === 1 ? "x" : o === 2 ? "+" : "";
        el.style.background = o >= 0 ? "" : colourFor(p);
        el.style.outline = revealed && truth[i] ? "2px solid var(--series-3)" : "";
        el.style.outlineOffset = revealed && truth[i] ? "-3px" : "";
        el.setAttribute("aria-label",
          String.fromCharCode(65 + c) + (r + 1) + ", " + (p * 100).toFixed(1) + " percent");
        cellsHtml += "<td>" + (p * 100).toFixed(1) + "</td>";
      }
      rows += "<tr>" + cellsHtml + "</tr>";
    }
    mirrorBody.innerHTML = rows;

    const omega = data.omega[turn];
    const bits = omega > 0 ? Math.log2(omega) : 0;
    const shot = turn > 0 ? cells[turn - 1] : -1;
    const name = shot >= 0
      ? String.fromCharCode(65 + (shot % W)) + (Math.floor(shot / W) + 1)
      : "";
    const kind = turn > 0
      ? (outcomes[turn - 1] === 0 ? "miss" : outcomes[turn - 1] === 1 ? "hit" : "sunk")
      : "";
    readout.innerHTML =
      "<span>turn <b>" + turn + "</b> of " + (turns - 1) + "</span>" +
      (turn > 0 ? "<span>shot <b>" + name + "</b>, " + kind + "</span>" : "<span>the prior</span>") +
      "<span>&#124;&#937;&#124; <b>" + omega.toLocaleString() + "</b></span>" +
      "<span><b>" + bits.toFixed(2) + "</b> bits left</span>";
    slider.value = String(turn);
  }

  function goTo(t) {
    turn = Math.min(turns - 1, Math.max(0, t));
    draw();
  }

  // ---- interaction --------------------------------------------------------
  slider.addEventListener("input", function () { goTo(parseInt(slider.value, 10)); });

  function stop() {
    if (playing) { clearTimeout(playing); playing = null; }
    play.textContent = "Play";
  }

  // Turns per tick. Never negative: a step of 0 holds the frame, which is what
  // +0 means, and anything below that would run the game backwards past its own
  // start. A number input's min attribute binds its spinner and not what can be
  // typed into it, so the value is clamped on read as well.
  function stepSize() {
    const v = Math.floor(Number(stepCtl.input.value));
    return isFinite(v) && v > 0 ? v : 0;
  }

  // Seconds per tick. Floored well above the frame rate so the frames cannot be
  // driven past what the eye can follow.
  function delaySeconds() {
    const v = Number(speedCtl.input.value);
    return isFinite(v) && v > 0.25 ? v : 0.25;
  }

  // A chain of timeouts rather than one interval, so a change to either control
  // is picked up without restarting playback.
  function tick() {
    if (turn >= turns - 1) { stop(); return; }
    goTo(turn + stepSize());
    playing = setTimeout(tick, delaySeconds() * 1000);
  }

  // Shortening the period should not mean waiting out the period it replaced, so
  // the pending tick is rescheduled against the new one.
  function rearm() {
    if (!playing) return;
    clearTimeout(playing);
    playing = setTimeout(tick, delaySeconds() * 1000);
  }

  // Written back on commit rather than on every keystroke, so typing "0.5" is
  // not rewritten to "0.25" at the moment the "0." is read.
  stepCtl.input.addEventListener("change", function () {
    stepCtl.input.value = String(stepSize());
  });
  speedCtl.input.addEventListener("change", function () {
    speedCtl.input.value = String(delaySeconds());
    rearm();
  });
  speedCtl.input.addEventListener("input", rearm);

  play.addEventListener("click", function () {
    if (playing) { stop(); return; }
    if (turn >= turns - 1) goTo(0);
    play.textContent = "Pause";
    playing = setTimeout(tick, delaySeconds() * 1000);
  });

  reveal.addEventListener("click", function () {
    revealed = !revealed;
    reveal.textContent = revealed ? "Hide fleet" : "Reveal fleet";
    draw();
  });

  root.addEventListener("keydown", function (e) {
    // The step and period boxes sit inside root, and there Left/Right/Home/End
    // are caret moves. Scrubbing on them would also preventDefault, so the
    // caret could not move at all. The range input keeps the handler: its
    // native keys move the thumb without pausing playback.
    const el = e.target;
    const tag = el && el.tagName ? String(el.tagName).toLowerCase() : "";
    if (tag === "input" && el.type !== "range") return;
    if (e.key === "ArrowRight") { stop(); goTo(turn + 1); e.preventDefault(); }
    else if (e.key === "ArrowLeft") { stop(); goTo(turn - 1); e.preventDefault(); }
    else if (e.key === "Home") { stop(); goTo(0); e.preventDefault(); }
    else if (e.key === "End") { stop(); goTo(turns - 1); e.preventDefault(); }
  });
  root.tabIndex = 0;

  draw();
})();
