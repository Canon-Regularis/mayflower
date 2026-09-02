"""How small could the engine's boundary state be?

The counting DP sweeps columns carrying a boundary state, so it is a
matrix-product contraction and the number of distinct boundary states is its bond
dimension. Whether that number is the smallest possible is checkable.

Cut the board between two columns. Every configuration splits into a left part
(the placements whose leftmost cell lies left of the cut) and a right part. Build

    M[l][r] = 1 if left part l and right part r together form a legal fleet

over distinct left and right parts. Three numbers come out of that matrix, and
they are not the same number:

  rank(M)         the Schmidt rank. Any linear matrix-product representation
                  needs at least this many. Achieving it may require negative or
                  fractional weights, which a counting sweep cannot use.

  distinct rows   two left parts with identical rows admit exactly the same
                  completions, so a counting sweep may merge them and add their
                  counts. This is the Myhill-Nerode state count, and it is the
                  smallest a state-based counting sweep can be.

  engine states   what the engine actually carries: the residual extension per
                  row plus the fleet-usage counter.

The gap that matters is the last two. rank(M) is reported as the floor beneath
both.

Rank is taken over a large prime field, so it is exact. A rank computed mod p can
only understate the rank over the rationals, so it stays a valid floor.

    python python/bond_dimension.py
"""

from __future__ import annotations

import argparse
import sys

from oracle import all_boards

PRIME = (1 << 61) - 1


def rank_mod_p(rows, p=PRIME):
    """Gaussian elimination over GF(p). `rows` holds dicts column -> value."""
    pivots = {}
    rank = 0
    for original in rows:
        row = dict(original)
        while row:
            col = min(row)
            if col not in pivots:
                inv = pow(row[col], p - 2, p)
                pivots[col] = {c: (v * inv) % p for c, v in row.items()}
                rank += 1
                break
            factor = row[col]
            for c, v in pivots[col].items():
                nv = (row.get(c, 0) - factor * v) % p
                if nv:
                    row[c] = nv
                elif c in row:
                    del row[c]
    return rank


def split(board, cut):
    left, right = [], []
    for ship in board:
        (left if min(c for _, c in ship) < cut else right).append(ship)
    return frozenset(left), frozenset(right)


def boundary_state(left, cut, height):
    """Exactly what the engine carries across a column boundary."""
    residual = [0] * height
    for ship in left:
        cols = [c for _, c in ship]
        if max(cols) >= cut:
            row = next(r for r, _ in ship)
            residual[row] = max(cols) - cut + 1
    return (tuple(residual), tuple(sorted(len(s) for s in left)))


def analyse(width, height, fleet, verbose=True):
    boards = all_boards(width, height, fleet)
    if verbose:
        print(f"{width}x{height} {fleet}: {len(boards):,} configurations")
        print(f"{'cut':>4} {'rank(M)':>9} {'nerode':>8} {'engine':>8} {'engine/nerode':>14}")

    rows_out = []
    for cut in range(1, width):
        pairs = {split(b, cut) for b in boards}
        lefts = sorted({l for l, _ in pairs}, key=lambda s: sorted(map(sorted, s)))
        rights = sorted({r for _, r in pairs}, key=lambda s: sorted(map(sorted, s)))
        rindex = {r: i for i, r in enumerate(rights)}

        by_left = {l: {} for l in lefts}
        for l, r in pairs:
            by_left[l][rindex[r]] = 1

        rank = rank_mod_p(by_left.values())
        nerode = len({frozenset(v) for v in by_left.values()})
        engine = len({boundary_state(l, cut, height) for l in lefts})

        # The engine is allowed to be finer than Nerode and never coarser. Two
        # left parts it maps to one boundary state have their counts added
        # together, so if their completion sets differed the sweep would be
        # counting boards that do not exist. This is the direction that is an
        # engine bug rather than a missed optimisation, so it is checked rather
        # than reported.
        completions = {}
        for left in lefts:
            state = boundary_state(left, cut, height)
            row = frozenset(by_left[left])
            if state in completions and completions[state] != row:
                raise AssertionError(
                    "cut {}: the engine merges two left parts whose completions "
                    "differ, so their counts must not be added".format(cut))
            completions[state] = row

        # rank(M) over GF(p) understates the rank over Q, so it is a floor under
        # both counts and can never exceed either.
        if not rank <= nerode <= engine:
            raise AssertionError(
                "cut {}: expected rank <= nerode <= engine, got {} {} {}".format(
                    cut, rank, nerode, engine))

        rows_out.append((cut, rank, nerode, engine))
        if verbose:
            print(f"{cut:>4} {rank:>9,} {nerode:>8,} {engine:>8,} "
                  f"{engine / nerode:>14.2f}")
    return rows_out


def self_test():
    """A case small enough to reason about by hand. One length-4 ship on a 4x4
    board has eight placements. Cut after the first column: the four horizontal
    ships all start at column 0 and cross, the vertical at column 0 does not, and
    the three other verticals sit entirely to the right. Since no second ship
    exists, every left part that already holds the ship behaves identically going
    forward, so two classes suffice: fleet placed, or not."""
    rows = analyse(4, 4, [4], verbose=False)
    cut, rank, nerode, engine = rows[0]
    ok = (rank == 2 and nerode == 2 and engine == 6)
    print(f"self-test, 4x4 with a single 4-ship at cut 1: rank {rank}, nerode {nerode}, "
          f"engine {engine}  {'OK' if ok else '*** UNEXPECTED ***'}")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    if not self_test():
        return 1
    print()

    cases = [(4, 4, [3, 2]), (4, 4, [2, 2]), (5, 4, [3, 2])]
    if not args.quick:
        cases += [(5, 5, [3, 2, 2]), (5, 5, [4, 3, 2])]

    worst = 1.0
    for w, h, f in cases:
        for cut, rank, nerode, engine in analyse(w, h, f):
            worst = max(worst, engine / nerode)
        print()

    print(f"Largest engine-to-Nerode ratio seen: {worst:.2f}.")
    if worst > 1.01:
        print("The boundary state the engine carries is not the coarsest one that")
        print("counts correctly. Distinct states exist whose completions coincide, so a")
        print("sweep could merge them and add their counts. Whether that merge is")
        print("computable locally, instead of by first enumerating completions, is the")
        print("open part: the engine's state is a sufficient statistic it can compute")
        print("from the cells it has already swept, and a Nerode class is not.")
    else:
        print("The engine already carries the coarsest correct boundary state.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
