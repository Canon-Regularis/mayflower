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
  // Ten, not twenty. The twenty came from Instance::validate(), where ext is
  // a uint64 and twenty rows of three bits genuinely fit. Here ext is built
  // with bitwise operators and stored in an Int32Array, so only ten rows fit
  // and the file's own header says so. Above ten the shifts wrap: at row 11
  // `1 << 33` is `1 << 1` and corrupts row 1's digit, and at row 10 a digit
  // of 2 or more reaches the sign bit and reads back as something else.
  //
  // The result was not a conservative undercount. 5x11 {5,4} returned 2175
  // against a true 2170, so the engine reported configurations that do not
  // exist. Measured across every width that fits 128 cells: no failures at
  // height 9 or 10, 7 of 80 at height 11, 50 of 72 at height 12.
  if (height > 10)
    throw new RangeError(
      `profile sweep supports height <= 10 in this engine, got ${height}`);

  const lengths = [...new Set(fleet)].sort((a, b) => a - b);
  const caps = lengths.map(L => fleet.filter(x => x === L).length);
  const stride = [];
  let fleetStates = 1;
  for (let i = 0; i < lengths.length; i++) { stride.push(fleetStates); fleetStates *= caps[i] + 1; }
  // aux packs the fleet index beside vrem in AUX_SHIFT bits, so a fleet with
  // more usage states than that has nowhere to go. Above the cap this engine
  // returned a wrong count rather than refusing, which is exactly the silent
  // zero the checks above were written to prevent. The C++ is unaffected:
  // its aux is a uint32 leaving 2^29 states, and fastPathSupports measures
  // the packed width separately.
  if (fleetStates > AUX_MASK + 1)
    throw new RangeError(
      `fleet needs ${fleetStates} usage states, more than the ${AUX_MASK + 1} the key holds`);
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

  // Double and rehash. The C++ counterpart grows at the same load factor
  // (src/core/detail/v0_sweep.hpp); this port kept the open addressing and
  // dropped the growth, so a full table made add() probe every slot, find
  // none free, and loop forever. That is reachable from the exported API:
  // makeInstance(10, 10, [8,7,6,5,4]) is accepted and passes four million
  // live states through a table of 2^20, which hangs the tab with no error.
  //
  // epoch restarts at 1 rather than 0 because the fresh stamp array is
  // zero-filled, and at epoch 0 every slot would read as live holding key 0.
  grow() {
    const oldExt = this.ext, oldAux = this.aux, oldCnt = this.cnt;
    const oldDense = this.dense, oldN = this.n;
    const cap = (this.mask + 1) * 2;
    this.mask = cap - 1;
    this.ext = new Int32Array(cap);
    this.aux = new Int32Array(cap);
    this.cnt = new Float64Array(cap);
    this.stamp = new Int32Array(cap);
    this.dense = new Int32Array(cap);
    this.n = 0;
    this.epoch = 1;
    for (let k = 0; k < oldN; k++) {
      const i = oldDense[k];
      this.add(oldExt[i], oldAux[i], oldCnt[i]);
    }
  }

  add(ext, aux, count) {
    // Grow before probing, so the probe below always has a free slot to
    // find. Seven tenths is the C++ threshold.
    if (this.n * 10 >= (this.mask + 1) * 7) this.grow();
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
/**
 * Everything the transition needs about one cell.
 *
 * These six values were passed positionally at four call sites, and the two
 * gate lookups were recomputed beside each of them. The C++ calls this CellCtx
 * and builds it once per cell; see src/core/detail/cell_ctx.hpp.
 */
function cellCtx(inst, cells, gate, row, col) {
  const c = row * inst.width + col;
  const cc = cells[c];
  return {
    row, col,
    mustEmpty: cc === EMPTY,
    mustOcc: cc === OCCUPIED,
    allowH: gate ? gate.h[c] : null,
    allowV: gate ? gate.v[c] : null,
  };
}

function expand(inst, ext, aux, ctx, emit) {
  const { row, col, mustEmpty, mustOcc, allowH, allowV } = ctx;
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
// What count() and marginals() both require of their arguments.
//
// Neither checked. A cells array shorter than the board read undefined past
// its end, and undefined !== EMPTY and !== OCCUPIED, so the tail silently
// counted as FREE: a one-element array returned the same 204 on 4x4 {3,2} as
// the full sixteen. A gate with missing or short arrays is worse, because
// expand tests (!allowH || allowH[li]) and a missing entry reads as allowed,
// so a malformed gate returns the unconstrained 264 rather than refusing.
// src/core/detail/entry.hpp does this check before every C++ sweep.
function checkArgs(inst, cells, gate) {
  if (cells && cells.length !== inst.cells)
    throw new RangeError(
      `cell filter has ${cells.length} entries, need ${inst.cells}`);
  if (!gate) return;
  // gate.h and gate.v are indexed by cell, and each entry is either null
  // or a per-length array, which is how cellCtx reads them.
  for (const [name, table] of [["h", gate.h], ["v", gate.v]]) {
    if (table === undefined || table === null) continue;
    if (table.length !== inst.cells)
      throw new RangeError(
        `${name} placement gate has ${table.length} entries, need ${inst.cells}`);
    for (let c = 0; c < table.length; c++)
      if (table[c] && table[c].length !== inst.lengths.length)
        throw new RangeError(
          `${name} gate at cell ${c} has ${table[c].length} lengths, ` +
          `need ${inst.lengths.length}`);
  }
}

// Refuse a layer whose sum has left the exactly-representable integers.
//
// The header's claim that every count here is exact is derived for the one
// standard instance, where the largest layer sum measures 1.58e10 against a
// limit of 9.01e15. makeInstance permits many others: 10x10 with twelve
// 2-ships returned 184521737660311420 where the true count ends 383, a
// silently rounded answer.
//
// src/core/profile_dp.cpp answers this by returning the value beside a flag,
// because CountResult is a struct. count() returns a primitive, so the choice
// here is between a rounded number and a refusal, and a refusal is the one a
// caller cannot mistake for an answer. Same detector as the C++: no value in
// the next layer can exceed this layer's sum, so a sum inside the limit means
// nothing in the layer it feeds has rounded.
const EXACT_LIMIT = 9007199254740992;   // 2^53

function checkExact(layer, where) {
  let sum = 0;
  for (let k = 0; k < layer.n; k++) sum += layer.cnt[layer.dense[k]];
  if (sum > EXACT_LIMIT)
    throw new RangeError(
      `${where}: a layer sums to ${sum}, past the 2^53 a double holds exactly, ` +
      `so this instance cannot be counted exactly here`);
}

export function count(inst, cells, gate) {
  checkArgs(inst, cells, gate);
  const cap = 1 << 20;
  let cur = new Layer(cap), next = new Layer(cap);
  cur.clear(); next.clear();
  cur.add(0, 0, 1);

  for (let col = 0; col < inst.width; col++) {
    for (let row = 0; row < inst.height; row++) {
      const c = row * inst.width + col;
      const cc = cells[c];
      const ctx = cellCtx(inst, cells, gate, row, col);
      next.clear();
      checkExact(cur, "count");
      for (let k = 0; k < cur.n; k++) {
        const i = cur.dense[k];
        const e = cur.ext[i], a = cur.aux[i], n = cur.cnt[i];
        expand(inst, e, a, ctx,
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
  checkArgs(inst, cells, gate);
  const cap = 1 << 20;
  const W = inst.width, H = inst.height;
  let cur = new Layer(cap), next = new Layer(cap);
  cur.clear(); next.clear();
  cur.add(0, 0, 1);

  const boundary = [cur.snapshot()];
  for (let col = 0; col < W; col++) {
    for (let row = 0; row < H; row++) {
      const c = row * W + col, cc = cells[c];
      const ctx = cellCtx(inst, cells, gate, row, col);
      next.clear();
      checkExact(cur, "count");
      for (let k = 0; k < cur.n; k++) {
        const i = cur.dense[k];
        const e = cur.ext[i], a = cur.aux[i], n = cur.cnt[i];
        expand(inst, e, a, ctx,
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
      const ctx = cellCtx(inst, cells, gate, row, col);
      b.clear();
      for (let k = 0; k < a.n; k++) {
        const i = a.dense[k];
        const e = a.ext[i], au = a.aux[i], n = a.cnt[i];
        expand(inst, e, au, ctx,
               (ne, na) => b.add(ne, na, n));
      }
      const t = a; a = b; b = t;
    }
    for (let row = H - 1; row >= 0; row--) {
      const c = row * W + col, cc = cells[c];
      const ctx = cellCtx(inst, cells, gate, row, col);
      const F = fLayers[row];
      let emptyFlow = 0;
      bCur.clear();
      for (let k = 0; k < F.ext.length; k++) {
        const e = F.ext[k], au = F.aux[k], n = F.cnt[k];
        let completions = 0;
        expand(inst, e, au, ctx, (ne, na) => {
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
  // Int32Array, not Uint8Array. A byte matched the announced length modulo
  // 256, so SUNK(258) was accepted wherever SUNK(2) is: on 4x4 {3,2} both
  // returned 12 where the C++, which holds this as an int, refuses. It is
  // the same narrowing hazard this file documents twice elsewhere.
  const sunkLen = new Int32Array(inst.cells);

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
    if (s.outcome === SUNK && !inst.fleet.includes(s.length))
      throw new RangeError(
        `shot ${t} announces SUNK(${s.length}), a length this fleet does not have`);
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
