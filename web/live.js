// The live engine widget.
//
// Two regimes, because the cost curves are opposite. Early on the posterior
// spans billions of configurations and the exact sweep takes tens of seconds in
// a browser, while a uniform sample still holds hundreds of thousands of
// survivors and is accurate. Late on the sample is exhausted while the exact
// sweep has become cheap. The widget uses whichever is sound and says which.
//
// The handoff is on the survivor count alone, which is what makes the two
// regimes complementary: the sample runs out only when the record is highly
// constraining, and a highly constraining record is a cheap sweep. Measured on
// this machine, every reachable state below the threshold sweeps in 2.0 to 3.4 s,
// and every state that costs more than that (a lone ship sunk in the opening,
// 3.8 to 4.3 s) still holds over fifteen hundred survivors.
//
// An earlier version also forced the sampled branch for the first six shots, to
// keep the expensive opening sweep out of the browser. That inverted the
// guarantee. Sinking the 2-ship and a 3-ship in the first five shots leaves 12
// survivors of 200,000, and the widget drew a posterior quantised to twelfths
// and fired on it, while the exact sweep it declined to run costs 2.0 s.

(function () {
  const LENS = [5, 4, 3, 3, 2];
  const W = 10, H = 10, CELLS = 100;
  const SWITCH_TO_EXACT = 400;   // survivors below this and the sample is spent

  const root = document.getElementById("live");
  if (!root) return;

  // ---- pool ---------------------------------------------------------------
  const raw = atob(root.dataset.pool);
  const pool = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) pool[i] = raw.charCodeAt(i);
  const NBOARDS = (pool.length / LENS.length) | 0;

  // Placement index -> cells, matching the exporter's formula.
  function placementCells(idx, L) {
    const hcount = H * (W - L + 1);
    const out = new Array(L);
    if (idx < hcount) {
      const r = (idx / (W - L + 1)) | 0, c = idx % (W - L + 1);
      for (let k = 0; k < L; k++) out[k] = r * W + c + k;
    } else {
      const j = idx - hcount;
      const c = (j / (H - L + 1)) | 0, r = j % (H - L + 1);
      for (let k = 0; k < L; k++) out[k] = (r + k) * W + c;
    }
    return out;
  }

  // Stamped ownership map, so loading a board costs 17 writes and no clear.
  const own = new Int8Array(CELLS), stamp = new Int32Array(CELLS);
  let gen = 0;
  function loadBoard(bi) {
    gen++;
    const base = bi * LENS.length;
    for (let j = 0; j < LENS.length; j++)
      for (const c of placementCells(pool[base + j], LENS[j])) { own[c] = j; stamp[c] = gen; }
  }
  const ownerOf = c => (stamp[c] === gen ? own[c] : -1);

  // ---- state --------------------------------------------------------------
  let truth = 0, history = [], survivors = [], posterior = new Float64Array(CELLS);
  let omega = 0, exactMode = false, reveal = false, playing = false, finished = false;
  // The pending tick, so a pause can cancel it. Without the handle a paused
  // chain stays scheduled, and pressing Play again inside one period leaves it
  // running beside the new one, each toggle adding another recompute per tick.
  let playTimer = null;

  const MISSV = 0, HITV = 1, SUNKV = 2;

  function consistent(bi) {
    loadBoard(bi);
    const rem = [5, 4, 3, 3, 2];
    for (const s of history) {
      const o = ownerOf(s.cell);
      if (o < 0) { if (s.outcome !== MISSV) return false; continue; }
      if (s.outcome === MISSV) return false;
      if (--rem[o] === 0) {
        if (s.outcome !== SUNKV || s.length !== LENS[o]) return false;
      } else if (s.outcome !== HITV) return false;
    }
    return true;
  }

  function recompute() {
    survivors = [];
    for (let bi = 0; bi < NBOARDS; bi++) if (consistent(bi)) survivors.push(bi);

    if (survivors.length >= SWITCH_TO_EXACT) {
      exactMode = false;
      posterior = new Float64Array(CELLS);
      for (const bi of survivors) {
        loadBoard(bi);
        for (let c = 0; c < CELLS; c++) if (stamp[c] === gen) posterior[c]++;
      }
      const n = survivors.length || 1;
      for (let c = 0; c < CELLS; c++) posterior[c] /= n;
      omega = survivors.length / NBOARDS * 15046987768;
      return;
    }
    // Sample spent: run the exact sweep, which is cheap by now.
    exactMode = true;
    const { cells, gate } = window.MayflowerEngine.constrain(INSTANCE, history);
    const m = window.MayflowerEngine.marginals(INSTANCE, cells, gate);
    omega = m.total;
    posterior = new Float64Array(CELLS);
    if (m.total > 0) for (let c = 0; c < CELLS; c++) posterior[c] = m.occ[c] / m.total;
  }

  const INSTANCE = window.MayflowerEngine.makeInstance(W, H, LENS);

  function newGame() {
    truth = (Math.random() * NBOARDS) | 0;
    history = []; finished = false; playing = false;
    recompute();
    render();
  }

  function step() {
    if (finished) return;
    const shot = new Uint8Array(CELLS);
    for (const s of history) shot[s.cell] = 1;
    let best = -1, bv = -1;
    for (let c = 0; c < CELLS; c++)
      if (!shot[c] && posterior[c] > bv) { bv = posterior[c]; best = c; }
    if (best < 0) { finished = true; return render(); }

    loadBoard(truth);
    const o = ownerOf(best);
    if (o < 0) history.push({ cell: best, outcome: MISSV });
    else {
      let hits = 1;
      for (const s of history) if (s.outcome !== MISSV) { loadBoard(truth); if (ownerOf(s.cell) === o) hits++; }
      loadBoard(truth);
      if (hits === LENS[o]) history.push({ cell: best, outcome: SUNKV, length: LENS[o] });
      else history.push({ cell: best, outcome: HITV });
    }
    const hitCount = history.filter(s => s.outcome !== MISSV).length;
    if (hitCount >= 17) { finished = true; playing = false; }
    recompute();
    render();
  }

  // ---- rendering ----------------------------------------------------------
  const boardEl = root.querySelector(".liveboard");
  const statEl = root.querySelector(".livestats");
  const btnNew = root.querySelector('[data-act="new"]');
  const btnStep = root.querySelector('[data-act="step"]');
  const btnPlay = root.querySelector('[data-act="play"]');
  const btnReveal = root.querySelector('[data-act="reveal"]');

  function render() {
    const shotAt = new Map();
    history.forEach((s, i) => shotAt.set(s.cell, { ...s, turn: i + 1 }));
    loadBoard(truth);
    const isShip = c => stamp[c] === gen;
    const hi = Math.max(...posterior, 1e-9);

    let html = "";
    for (let c = 0; c < CELLS; c++) {
      const s = shotAt.get(c);
      const p = posterior[c];
      const bucket = Math.min(12, Math.max(0, Math.round(p / hi * 12)));
      let cls = "lc", label = "";
      if (s) {
        cls += s.outcome === MISSV ? " miss" : (s.outcome === SUNKV ? " sunk" : " hit");
        label = s.outcome === MISSV ? "." : (s.outcome === SUNKV ? "x" : "o");
      } else {
        label = p > 0 ? (p * 100).toFixed(0) : "";
      }
      if (!s && reveal && isShip(c)) cls += " ghost";
      const last = history.length && history[history.length - 1].cell === c ? " last" : "";
      const style = s ? "" : ` style="background:var(--ramp-${bucket})"`;
      html += `<div class="${cls}${last}"${style} title="${String.fromCharCode(65 + c % W)}${(c / W | 0) + 1}: ${(p * 100).toFixed(1)}%">${label}</div>`;
    }
    boardEl.innerHTML = html;

    const hits = history.filter(s => s.outcome !== MISSV).length;
    statEl.innerHTML =
      `<div><span class="lk">shots</span><span class="lv">${history.length}</span></div>` +
      `<div><span class="lk">hits</span><span class="lv">${hits} / 17</span></div>` +
      `<div><span class="lk">boards still possible</span><span class="lv">${Math.round(omega).toLocaleString()}</span></div>` +
      `<div><span class="lk">posterior</span><span class="lv ${exactMode ? "exact" : "sampled"}">${exactMode ? "exact sweep" : "sampled, " + survivors.length.toLocaleString() + " of " + NBOARDS.toLocaleString()}</span></div>`;

    btnStep.disabled = finished;
    btnPlay.disabled = finished;
    btnPlay.textContent = playing ? "Pause" : "Play";
    btnReveal.textContent = reveal ? "Hide fleet" : "Reveal fleet";
  }

  btnNew.addEventListener("click", newGame);
  btnStep.addEventListener("click", step);
  btnReveal.addEventListener("click", () => { reveal = !reveal; render(); });
  btnPlay.addEventListener("click", () => {
    playing = !playing;
    render();
    if (playTimer) { clearTimeout(playTimer); playTimer = null; }
    const tick = () => {
      playTimer = null;
      if (!playing || finished) { playing = false; return render(); }
      step();
      if (playing && !finished) playTimer = setTimeout(tick, 260);
      else render();
    };
    if (playing) tick();
  });

  newGame();
})();
