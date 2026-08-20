"""Order-aware brute-force reference.

Ground truth for the C++ engine's observation semantics.

SUNK(x, L) means the shot at cell x sank the ship, so every other cell of that
ship was already shot strictly before x. Requiring only that cells(ship) be a
subset of HIT admits configurations that could not have produced the
announcement, and over-counts. The posterior is therefore a function of the
ordered history; permuting hits among themselves can change it.

    python python/oracle.py
    python python/oracle.py --order-dependence
"""

from __future__ import annotations

import argparse
import random
import sys

MISS, HIT, SUNK = "MISS", "HIT", "SUNK"


# --------------------------------------------------------------------------- #
# Enumeration
# --------------------------------------------------------------------------- #

def placements(width, height, length):
    """Every legal placement of a length-L ship, as a frozenset of cells."""
    out = []
    for r in range(height):
        for c in range(width - length + 1):
            out.append(frozenset((r, c + k) for k in range(length)))
    if length > 1:
        for c in range(width):
            for r in range(height - length + 1):
                out.append(frozenset((r + k, c) for k in range(length)))
    return out


def all_boards(width, height, fleet):
    """Every physical board. Increasing placement indices within a length group
    make equal-length ships interchangeable."""
    groups = []
    for length in sorted(set(fleet)):
        groups.append((placements(width, height, length), fleet.count(length)))

    boards = []

    def go(gi, chosen, occupied):
        if gi == len(groups):
            boards.append(tuple(chosen))
            return
        options, multiplicity = groups[gi]

        def pick(need, start, occ):
            if need == 0:
                go(gi + 1, chosen, occ)
                return
            for i in range(start, len(options)):
                p = options[i]
                if occ & p:
                    continue
                chosen.append(p)
                pick(need - 1, i + 1, occ | p)
                chosen.pop()

        pick(multiplicity, 0, occupied)

    go(0, [], frozenset())
    return boards


# --------------------------------------------------------------------------- #
# Observation model
# --------------------------------------------------------------------------- #

def simulate(board, shots):
    """Play `shots` against `board`. A ship reports SUNK on the shot that
    completes it, and only then."""
    remaining = [len(s) for s in board]
    outcomes = []
    seen = set()
    for cell in shots:
        if cell in seen:
            raise ValueError("cell %r shot twice; histories must not repeat cells" % (cell,))
        seen.add(cell)
        idx = None
        for i, ship in enumerate(board):
            if cell in ship:
                idx = i
                break
        if idx is None:
            outcomes.append((MISS, 0))
            continue
        remaining[idx] -= 1
        if remaining[idx] == 0:
            outcomes.append((SUNK, len(board[idx])))
        else:
            outcomes.append((HIT, 0))
    return outcomes


def posterior(boards, shots, outcomes):
    """Boards consistent with the ordered observation record."""
    want = list(outcomes)
    return [b for b in boards if simulate(b, shots) == want]


def posterior_order_free(boards, shots, outcomes):
    """The order-free predicate, kept so tests can assert that it disagrees with
    `posterior`. Looks only at which cells are hits or misses and which lengths
    were sunk."""
    misses = set(c for c, o in zip(shots, outcomes) if o[0] == MISS)
    hits = set(c for c, o in zip(shots, outcomes) if o[0] != MISS)
    sunk_lengths = sorted(o[1] for o in outcomes if o[0] == SUNK)
    out = []
    for b in boards:
        occupied = frozenset().union(*b) if b else frozenset()
        if occupied & misses:
            continue
        if not hits <= occupied:
            continue
        got = sorted(len(s) for s in b if s <= hits)
        if got != sunk_lengths:
            continue
        out.append(b)
    return out


# --------------------------------------------------------------------------- #
# Searches
# --------------------------------------------------------------------------- #

def find_order_dependence(width, height, fleet, trials=3000, seed=12345):
    """Two orderings of the same shots giving the same outcome multiset but
    different ordered posteriors."""
    rng = random.Random(seed)
    boards = all_boards(width, height, fleet)
    cells = [(r, c) for r in range(height) for c in range(width)]

    for _ in range(trials):
        truth = rng.choice(boards)
        k = rng.randint(4, min(10, len(cells)))
        shots = rng.sample(cells, k)
        outcomes = simulate(truth, shots)
        if not any(o[0] == SUNK for o in outcomes):
            continue   # without a sink, order cannot matter
        n_a = len(posterior(boards, shots, outcomes))
        for _ in range(12):
            perm = shots[:]
            rng.shuffle(perm)
            outcomes_b = simulate(truth, perm)
            if sorted(outcomes) != sorted(outcomes_b):
                continue   # different outcome multiset is not a fair comparison
            n_b = len(posterior(boards, perm, outcomes_b))
            if n_b != n_a:
                return shots, outcomes, n_a, perm, outcomes_b, n_b
    return None


def find_overcount(width, height, fleet, trials=3000, seed=999):
    """A history where the order-free predicate over-counts."""
    rng = random.Random(seed)
    boards = all_boards(width, height, fleet)
    cells = [(r, c) for r in range(height) for c in range(width)]
    for _ in range(trials):
        truth = rng.choice(boards)
        k = rng.randint(4, min(10, len(cells)))
        shots = rng.sample(cells, k)
        outcomes = simulate(truth, shots)
        if not any(o[0] == SUNK for o in outcomes):
            continue
        exact = len(posterior(boards, shots, outcomes))
        naive = len(posterior_order_free(boards, shots, outcomes))
        if naive != exact:
            return shots, outcomes, exact, naive
    return None


def fmt(shots, outcomes):
    parts = []
    for c, o in zip(shots, outcomes):
        tag = o[0] if o[0] != SUNK else "SUNK%d" % o[1]
        parts.append("%s:%s" % (c, tag))
    return " ".join(parts)


# --------------------------------------------------------------------------- #

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--order-dependence", action="store_true")
    args = ap.parse_args()

    ladder = [(4, 4, [3, 2]), (5, 5, [3, 2, 2]), (5, 5, [4, 3, 2]),
              (6, 6, [3, 3, 2]), (5, 5, [3, 3, 2, 2])]

    print("board counts (must match the C++ DP exactly)")
    for w, h, f in ladder:
        print("  %dx%d %s: %s" % (w, h, f, format(len(all_boards(w, h, f)), ",")))

    if args.order_dependence:
        print("\n[1] the ordered posterior depends on shot order")
        for w, h, f in [(5, 5, [3, 2, 2]), (5, 4, [3, 3, 2]), (5, 5, [4, 3, 2])]:
            res = find_order_dependence(w, h, f)
            if res:
                a, oa, na, b, ob, nb = res
                print("  INSTANCE %dx%d %s" % (w, h, f))
                print("    order A  %s" % fmt(a, oa))
                print("             |Omega| = %d" % na)
                print("    order B  %s" % fmt(b, ob))
                print("             |Omega| = %d" % nb)
                print("    Same shots, same outcome multiset, different posterior.")
                print("    Any cache key or predicate that ignores order is unsound.")
                break
        else:
            print("  none found in the trial budget")

        print("\n[2] the order-free predicate over-counts")
        for w, h, f in [(5, 5, [3, 2, 2]), (5, 4, [3, 3, 2]), (5, 5, [4, 3, 2])]:
            res = find_overcount(w, h, f)
            if res:
                shots, outcomes, exact, naive = res
                print("  INSTANCE %dx%d %s" % (w, h, f))
                print("    history  %s" % fmt(shots, outcomes))
                print("    ordered     |Omega| = %d" % exact)
                print("    order-free  |Omega| = %d   (over by %d)" % (naive, naive - exact))
                break
        else:
            print("  none found in the trial budget")
    return 0


if __name__ == "__main__":
    sys.exit(main())
