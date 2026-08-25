// The belief scrubber.
//
// One recorded game, replayed a shot at a time, with the exact posterior at
// every turn. The frames are precomputed and quantised to a byte per cell, so
// nothing is recounted in the browser: scrubbing is a lookup and stays smooth
// under the keyboard.
//
// The board is the same widget used everywhere else on the page, with the same
// glyph vocabulary: open ring for a miss, filled disc for a hit, cross for the
// shot that sank a ship, and a dashed outline for the hidden truth once it is
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

  const reveal = document.createElement("button");
  reveal.type = "button";
  reveal.className = "scrubbtn";
  reveal.textContent = "Reveal fleet";

  controls.appendChild(play);
  controls.appendChild(slider);
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
        el.style.outline = revealed && truth[i] ? "2px dashed var(--ink)" : "";
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
    if (playing) { clearInterval(playing); playing = null; }
    play.textContent = "Play";
  }

  play.addEventListener("click", function () {
    if (playing) { stop(); return; }
    if (turn >= turns - 1) goTo(0);
    play.textContent = "Pause";
    playing = setInterval(function () {
      if (turn >= turns - 1) { stop(); return; }
      goTo(turn + 1);
    }, 420);
  });

  reveal.addEventListener("click", function () {
    revealed = !revealed;
    reveal.textContent = revealed ? "Hide fleet" : "Reveal fleet";
    draw();
  });

  root.addEventListener("keydown", function (e) {
    if (e.key === "ArrowRight") { stop(); goTo(turn + 1); e.preventDefault(); }
    else if (e.key === "ArrowLeft") { stop(); goTo(turn - 1); e.preventDefault(); }
    else if (e.key === "Home") { stop(); goTo(0); e.preventDefault(); }
    else if (e.key === "End") { stop(); goTo(turns - 1); e.preventDefault(); }
  });
  root.tabIndex = 0;

  if (window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    play.disabled = true;
    play.title = "Autoplay is off because the system asks for reduced motion";
  }

  draw();
})();
