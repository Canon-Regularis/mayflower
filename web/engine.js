// Mayflower, in the browser.
//
// The same broken-profile transfer-matrix DP the C++ engine runs, ported to
// JavaScript so the page computes the real posterior instead of replaying a
// recording.
//
// Numeric safety: the profile needs 3 bits per row (30 for a 10-row board) and
// so fits an int32, which keeps the bitwise operators usable. Counts reach
// 17 * 15,046,987,768 = 2.56e11, comfortably inside the 2^53 integers a double
// represents exactly, so every count here is exact.
//
// Constraints enter as a per-cell filter plus a per-placement gate. The gate is
// what makes sunk announcements correct: SUNK(x, L) means the shot at x sank the
// ship, so every other cell of it was already shot, and a predicate that only
// checks membership of the hit set over-counts.

export const MISS = 0, HIT = 1, SUNK = 2;

export function makeInstance(width, height, fleet) {
  // The same rules Instance::validate() applies in the C++. Three
  // implementations that are meant to agree should refuse the same inputs, and
  // this one answered 0 for a zero-length ship, 0 for a ship longer than the
  // board, and threw "Invalid typed array length: -16" on a negative dimension.
  // A silent 0 from one of three engines is the worst of the three outcomes,
  // because it looks like a legitimate count.
  if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0)
    throw new RangeError(`board dimensions must be positive integers, got ${width}x${height}`);
  if (width * height > 128)
    throw new RangeError(`board is limited to 128 cells, got ${width * height}`);
  if (!Array.isArray(fleet) || fleet.length === 0)
    throw new RangeError("fleet must be a non-empty array");
  for (const L of fleet) {
    if (!Number.isInteger(L) || L < 1)
      throw new RangeError(`ship length must be a positive integer, got ${L}`);
    if (L > width && L > height)
      throw new RangeError(`ship of length ${L} does not fit on ${width}x${height}`);
    if (L > 8)
      throw new RangeError(`profile sweep supports ship length <= 8, got ${L}`);
  }
  if (height > 20)
    throw new RangeError(`profile sweep supports height <= 20, got ${height}`);

  const lengths = [...new Set(fleet)].sort((a, b) => a - b);
  const caps = lengths.map(L => fleet.filter(x => x === L).length);
  const stride = [];
  let fleetStates = 1;
  for (let i = 0; i < lengths.length; i++) { stride.push(fleetStates); fleetStates *= caps[i] + 1; }
  return {
    width, height, fleet, lengths, caps, stride, fleetStates,
    cells: width * height,
    shipCells: fleet.reduce((a, b) => a + b, 0),
    fullFleet: fleetStates - 1,
  };
}

// Open-addressed table over (ext, aux) with an epoch stamp, so clearing a layer
// costs one increment. Typed arrays throughout to keep the loop monomorphic.
class Layer {
  constructor(capacity) {
    this.mask = capacity - 1;
    this.ext = new Int32Array(capacity);
    this.aux = new Int32Array(capacity);
    this.cnt = new Float64Array(capacity);
    this.stamp = new Int32Array(capacity);
    this.dense = new Int32Array(capacity);
    this.n = 0;
    this.epoch = 0;
  }
  clear() { this.epoch++; this.n = 0; }
  add(ext, aux, count) {
    let i = (Math.imul(ext, 0x9e3779b1) ^ Math.imul(aux + 1, 0x85ebca6b)) & this.mask;
    for (;;) {
      if (this.stamp[i] !== this.epoch) {
        this.stamp[i] = this.epoch;
        this.ext[i] = ext; this.aux[i] = aux; this.cnt[i] = count;
        this.dense[this.n++] = i;
        return;
      }
      if (this.ext[i] === ext && this.aux[i] === aux) { this.cnt[i] += count; return; }
      i = (i + 1) & this.mask;
    }
  }
  get(ext, aux) {
    let i = (Math.imul(ext, 0x9e3779b1) ^ Math.imul(aux + 1, 0x85ebca6b)) & this.mask;
    for (;;) {
      if (this.stamp[i] !== this.epoch) return 0;
      if (this.ext[i] === ext && this.aux[i] === aux) return this.cnt[i];
      i = (i + 1) & this.mask;
    }
  }
  snapshot() {
    const out = { ext: new Int32Array(this.n), aux: new Int32Array(this.n), cnt: new Float64Array(this.n) };
    for (let k = 0; k < this.n; k++) {
      const i = this.dense[k];
      out.ext[k] = this.ext[i]; out.aux[k] = this.aux[i]; out.cnt[k] = this.cnt[i];
    }
    return out;
  }
  load(s) {
    this.clear();
    for (let k = 0; k < s.ext.length; k++) this.add(s.ext[k], s.aux[k], s.cnt[k]);
  }
}

// Per-cell constraints: 0 free, 1 must be empty, 2 must be occupied.
export const FREE = 0, EMPTY = 1, OCCUPIED = 2;

// Enumerate the successors of one state at one cell. Emission order is fixed,
// which is what keeps results reproducible.
function expand(inst, ext, aux, row, mustEmpty, mustOcc, allowH, allowV, col, emit) {
  const nL = inst.lengths.length;
  const vrem = aux >> AUX_SHIFT;
  const fleet = aux & AUX_MASK;
  const shift = 3 * row;
  const d = (ext >> shift) & 7;

  if (d > 0) {
    if (vrem > 0 || mustEmpty) return;
    emit(ext - (1 << shift), aux);
    return;
  }
  if (vrem > 0) {
    if (mustEmpty) return;
    emit(ext, aux - (1 << AUX_SHIFT));
    return;
  }
  if (!mustOcc) emit(ext, aux);
  if (mustEmpty) return;

  for (let li = 0; li < nL; li++) {
    const L = inst.lengths[li];
    const used = ((fleet / inst.stride[li]) | 0) % (inst.caps[li] + 1);
    if (used >= inst.caps[li]) continue;
    const nf = fleet + inst.stride[li];
    if (col + L <= inst.width && (!allowH || allowH[li]))
      emit(ext | ((L - 1) << shift), nf);
    // A length-1 ship has one placement, not two.
    if (L > 1 && row + L <= inst.height && (!allowV || allowV[li]))
      emit(ext, ((L - 1) << AUX_SHIFT) | nf);
  }
}

// aux = vrem << 5 | fleetIndex. The fleet index is under 32 for every fleet the
// browser engine handles, so both fields come out with shifts.
const AUX_SHIFT = 5, AUX_MASK = 31;

/** Exact configuration count under the given constraints. */
export function count(inst, cells, gate) {
  const cap = 1 << 20;
  let cur = new Layer(cap), next = new Layer(cap);
  cur.clear(); next.clear();
  cur.add(0, 0, 1);

  for (let col = 0; col < inst.width; col++) {
    for (let row = 0; row < inst.height; row++) {
      const c = row * inst.width + col;
      const cc = cells[c];
      const aH = gate ? gate.h[c] : null, aV = gate ? gate.v[c] : null;
      next.clear();
      for (let k = 0; k < cur.n; k++) {
        const i = cur.dense[k];
        const e = cur.ext[i], a = cur.aux[i], n = cur.cnt[i];
        expand(inst, e, a, row, cc === EMPTY, cc === OCCUPIED, aH, aV, col,
               (ne, na) => next.add(ne, na, n));
      }
      const t = cur; cur = next; next = t;
    }
  }
  let total = 0;
  for (let k = 0; k < cur.n; k++) {
    const i = cur.dense[k];
    if (cur.ext[i] === 0 && cur.aux[i] === inst.fullFleet) total += cur.cnt[i];
  }
  return total;
}

/**
 * Exact per-cell occupancy counts, by one forward sweep and one backward sweep.
 *
 * Every configuration either occupies a cell or leaves it empty, and the empty
 * transition is the identity on the state, so occupancy(cell) = total minus the
 * flow through that identity edge. Forward layers are kept only at the column
 * boundaries and replayed inside a column while the backward pass walks through
 * it, which holds the working set to one column.
 */
export function marginals(inst, cells, gate) {
  const cap = 1 << 20;
  const W = inst.width, H = inst.height;
  let cur = new Layer(cap), next = new Layer(cap);
  cur.clear(); next.clear();
  cur.add(0, 0, 1);

  const boundary = [cur.snapshot()];
  for (let col = 0; col < W; col++) {
    for (let row = 0; row < H; row++) {
      const c = row * W + col, cc = cells[c];
      const aH = gate ? gate.h[c] : null, aV = gate ? gate.v[c] : null;
      next.clear();
      for (let k = 0; k < cur.n; k++) {
        const i = cur.dense[k];
        const e = cur.ext[i], a = cur.aux[i], n = cur.cnt[i];
        expand(inst, e, a, row, cc === EMPTY, cc === OCCUPIED, aH, aV, col,
               (ne, na) => next.add(ne, na, n));
      }
      const t = cur; cur = next; next = t;
    }
    boundary.push(cur.snapshot());
  }
  let total = 0;
  for (let k = 0; k < cur.n; k++) {
    const i = cur.dense[k];
    if (cur.ext[i] === 0 && cur.aux[i] === inst.fullFleet) total += cur.cnt[i];
  }
  const occ = new Float64Array(inst.cells);
  if (total === 0) return { total: 0, occ };

  let bNext = new Layer(cap), bCur = new Layer(cap);
  bNext.clear(); bCur.clear();
  const last = boundary[W];
  for (let k = 0; k < last.ext.length; k++)
    if (last.ext[k] === 0 && last.aux[k] === inst.fullFleet) bNext.add(0, inst.fullFleet, 1);

  const replay = new Layer(cap), replayNext = new Layer(cap);
  const fLayers = new Array(H);

  for (let col = W - 1; col >= 0; col--) {
    replay.load(boundary[col]);
    let a = replay, b = replayNext;
    for (let row = 0; row < H; row++) {
      fLayers[row] = a.snapshot();
      const c = row * W + col, cc = cells[c];
      const aH = gate ? gate.h[c] : null, aV = gate ? gate.v[c] : null;
      b.clear();
      for (let k = 0; k < a.n; k++) {
        const i = a.dense[k];
        const e = a.ext[i], au = a.aux[i], n = a.cnt[i];
        expand(inst, e, au, row, cc === EMPTY, cc === OCCUPIED, aH, aV, col,
               (ne, na) => b.add(ne, na, n));
      }
      const t = a; a = b; b = t;
    }
    for (let row = H - 1; row >= 0; row--) {
      const c = row * W + col, cc = cells[c];
      const aH = gate ? gate.h[c] : null, aV = gate ? gate.v[c] : null;
      const F = fLayers[row];
      let emptyFlow = 0;
      bCur.clear();
      for (let k = 0; k < F.ext.length; k++) {
        const e = F.ext[k], au = F.aux[k], n = F.cnt[k];
        let completions = 0;
        expand(inst, e, au, row, cc === EMPTY, cc === OCCUPIED, aH, aV, col, (ne, na) => {
          const bb = bNext.get(ne, na);
          completions += bb;
          if (ne === e && na === au) emptyFlow += n * bb;   // the identity edge
        });
        if (completions) bCur.add(e, au, completions);
      }
      occ[c] = total - emptyFlow;
      const t = bCur; bCur = bNext; bNext = t;
    }
  }
  return { total, occ };
}

/**
 * Build the per-cell filter and the per-placement gate from an ordered history.
 * history: [{cell, outcome, length}] in the order the shots were fired.
 */
export function constrain(inst, history) {
  const W = inst.width, H = inst.height, nL = inst.lengths.length;
  const cells = new Uint8Array(inst.cells);
  const time = new Int32Array(inst.cells).fill(-1);
  const outcome = new Uint8Array(inst.cells);
  const sunkLen = new Uint8Array(inst.cells);

  // The same record rules History::add applies in the C++. Typed arrays ignore
  // an out-of-range write instead of throwing, so a shot at cell 999 simply
  // vanished and constrain returned the unconstrained posterior: 264 on
  // 4x4 {3,2}, exactly as if nothing had been shot. A caller conditioning on a
  // record it got wrong would have been shown the prior and told it was the
  // posterior.
  const seen = new Set();
  history.forEach((s, t) => {
    if (!Number.isInteger(s.cell) || s.cell < 0 || s.cell >= inst.cells)
      throw new RangeError(`shot ${t} is at cell ${s.cell}, off a ${inst.cells}-cell board`);
    if (seen.has(s.cell))
      throw new RangeError(`cell ${s.cell} is shot twice, at shot ${t}`);
    seen.add(s.cell);
    if (s.outcome !== MISS && s.outcome !== HIT && s.outcome !== SUNK)
      throw new RangeError(`shot ${t} carries outcome ${s.outcome}`);
    if (s.outcome === SUNK && !(Number.isInteger(s.length) && s.length >= 1))
      throw new RangeError(`shot ${t} announces SUNK without a ship length`);
    time[s.cell] = t;
    outcome[s.cell] = s.outcome;
    sunkLen[s.cell] = s.length || 0;
    cells[s.cell] = s.outcome === MISS ? EMPTY : OCCUPIED;
  });

  // A placement is allowed when, if every one of its cells has been shot, its
  // latest-shot cell carries SUNK with the matching length and the rest carry
  // plain hits; and otherwise no cell of it carries SUNK.
  const allows = (fp, L) => {
    let shot = 0, latest = -1, latestCell = -1;
    for (let k = 0; k < L; k++) {
      const c = fp[k];
      if (time[c] < 0) continue;
      if (outcome[c] === MISS) return false;
      shot++;
      if (time[c] > latest) { latest = time[c]; latestCell = c; }
    }
    if (shot === L) {
      if (outcome[latestCell] !== SUNK || sunkLen[latestCell] !== L) return false;
      for (let k = 0; k < L; k++)
        if (fp[k] !== latestCell && outcome[fp[k]] !== HIT) return false;
      return true;
    }
    for (let k = 0; k < L; k++)
      if (time[fp[k]] >= 0 && outcome[fp[k]] !== HIT) return false;
    return true;
  };

  const h = [], v = [], fp = new Int32Array(8);
  for (let row = 0; row < H; row++) {
    for (let col = 0; col < W; col++) {
      const c = row * W + col;
      const ah = new Uint8Array(nL), av = new Uint8Array(nL);
      for (let li = 0; li < nL; li++) {
        const L = inst.lengths[li];
        if (col + L <= W) {
          for (let k = 0; k < L; k++) fp[k] = row * W + col + k;
          ah[li] = allows(fp, L) ? 1 : 0;
        }
        if (row + L <= H) {
          for (let k = 0; k < L; k++) fp[k] = (row + k) * W + col;
          av[li] = allows(fp, L) ? 1 : 0;
        }
      }
      h[c] = ah; v[c] = av;
    }
  }
  return { cells, gate: { h, v } };
}
