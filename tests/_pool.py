"""The board-pool layout, decoded once.

tools/export_pool.cpp writes one byte per ship, holding that ship's placement
index. The index is the exporter's own, documented at the top of that file and
computed by placementIndexOf:

    horizontal at (row, col) -> row * (W-L+1) + col
    vertical   at (row, col) -> H*(W-L+1) + col * (H-L+1) + row

Four decoders of that one layout existed: web/live.js, which ships and stays,
plus three in this directory. They agreed, except that the one in
tests/test_pool.py allocated the vertical half unconditionally where the other
three guarded it with L > 1. A length-1 ship has one placement, not two. That
divergence is unreachable with {5,4,3,3,2} and it is the exact bug class
python/oracle.py keeps three length-1 ladder cases to catch, which is reason
enough not to keep four chances at it.

The widget's survivor rule lives here too, for the same reason: it was written
twice inside tests/test_engine_js.py, once returning a count and once inlined to
return per-cell counts as well.
"""

from __future__ import annotations

import io

W = H = 10
CELLS = W * H
LENS = [5, 4, 3, 3, 2]
SHIP_CELLS = sum(LENS)

# Outcome codes, matching web/engine.js and mayflower::Outcome.
MISS, HIT, SUNK = 0, 1, 2


def slot_count(L, w=W, h=H):
    """How many placement indices a length-L ship has."""
    return h * (w - L + 1) + (w * (h - L + 1) if L > 1 else 0)


def placement_cells(idx, L, w=W, h=H):
    """The cells a placement index covers."""
    hcount = h * (w - L + 1)
    if idx < hcount:
        r, c = divmod(idx, w - L + 1)
        return [r * w + c + k for k in range(L)]
    j = idx - hcount
    c, r = divmod(j, h - L + 1)
    return [(r + k) * w + c for k in range(L)]


def placement_table(L, w=W, h=H):
    """Every placement of a length-L ship, in index order."""
    return [placement_cells(i, L, w, h) for i in range(slot_count(L, w, h))]


def read_pool(path):
    """The raw bytes and the board count."""
    raw = io.open(path, "rb").read()
    return raw, len(raw) // len(LENS)


def board_owner(raw, bi):
    """cell -> which ship of board bi occupies it."""
    base = bi * len(LENS)
    owner = {}
    for j, L in enumerate(LENS):
        for c in placement_cells(raw[base + j], L):
            owner[c] = j
    return owner


def board_consistent(owner, history):
    """Whether one board satisfies the record, by the widget's own rule.

    Mirrors consistent() in web/live.js: a shot on no ship must be a miss, a
    shot on a ship must not be, and the shot that takes a ship's last cell must
    be SUNK carrying that ship's length.
    """
    rem = list(LENS)
    for cell, outcome, length in history:
        j = owner.get(cell)
        if j is None:
            if outcome != MISS:
                return False
            continue
        if outcome == MISS:
            return False
        rem[j] -= 1
        if rem[j] == 0:
            if outcome != SUNK or length != LENS[j]:
                return False
        elif outcome != HIT:
            return False
    return True


def survivors(raw, n, history):
    """(count, per-cell occupancy counts) over the boards the record allows."""
    alive = 0
    counts = [0] * CELLS
    for bi in range(n):
        owner = board_owner(raw, bi)
        if not board_consistent(owner, history):
            continue
        alive += 1
        for c in owner:
            counts[c] += 1
    return alive, counts
