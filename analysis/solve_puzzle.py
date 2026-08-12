#!/usr/bin/env python3
"""Solve the Jane Street ASIC puzzle from the extracted netlist.

Everything here is derived from the recovered netlist, never from the layout
image or any outside knowledge of the puzzle:

  1. probe the design one grid cell at a time to recover the region map,
  2. solve the resulting Star Battle,
  3. feed the solution back through the netlist and read the output generator.

Usage: scripts/solve_puzzle.py [path/to/puzzle.json]
"""
import json
import sys
from collections import defaultdict
from itertools import combinations
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pysim import Puzzle  # noqa: E402

N = 11                    # grid side, from the row/column counters
CELLS = N * N             # one input bit per cell
STARS = 2                 # stars per row / column / region (verified below)
SUCCESS_FF = 'dfrtp_2'    # the registered `success` output


def probe_regions(p):
    """Recover which counter each cell increments by toggling one cell at a time.

    Only the flops the success check actually reads are considered: the rest of
    the design (the output generator's LFSR, the neighbour pipeline) also reacts
    to a star and would otherwise be mistaken for extra constraint groups.
    """
    watch = sorted(p.support(p.ff[SUCCESS_FF]['connections']['D']) & set(p.ff))

    def final(bits):
        tr = p.run(bits, extra=2, capture=lambda P, s: [P.q(s, n) for n in watch])
        return tr[CELLS - 1]

    zero = final([0] * CELLS)
    sig = {}
    for t in range(CELLS):
        bits = [0] * CELLS
        bits[t] = 1
        f = final(bits)
        sig[t] = frozenset(watch[k] for k in range(len(watch)) if f[k] != zero[k])

    common = frozenset.intersection(*sig.values())   # the global star counter
    where = defaultdict(set)
    for t, s in sig.items():
        for f in s - common:
            where[f].add(t)

    column_of, region_of = {}, {}
    for f, cells in where.items():
        cols = {t % N for t in cells}
        if len(cols) == 1 and cells == {r * N + next(iter(cols)) for r in range(N)}:
            for t in cells:
                column_of[t] = f
        else:
            for t in cells:
                region_of[t] = f
    if len(region_of) != CELLS or len(column_of) != CELLS:
        raise SystemExit('probe failed: could not classify every cell')
    return [[region_of[r * N + c] for c in range(N)] for r in range(N)]


def solve_star_battle(region):
    names = sorted({c for row in region for c in row})
    rid = {n: k for k, n in enumerate(names)}
    left = [sum(row.count(n) for row in region) for n in names]
    colcnt = [0] * N
    regcnt = [0] * len(names)
    rows, out = [], []

    def rec(r, remaining):
        if r == N:
            out.append([tuple(x) for x in rows])
            return
        if any(STARS - colcnt[c] > N - r for c in range(N)):
            return
        if any(STARS - regcnt[k] > remaining[k] for k in range(len(names))):
            return
        prev = rows[-1] if rows else ()
        for combo in combinations(range(N), STARS):
            if any(b - a <= 1 for a, b in zip(combo, combo[1:])):
                continue                                  # adjacent within the row
            if any(abs(c - pc) <= 1 for c in combo for pc in prev):
                continue                                  # touches the row above
            if any(colcnt[c] >= STARS for c in combo):
                continue
            rr = [rid[region[r][c]] for c in combo]
            if len(set(rr)) == 1 and regcnt[rr[0]] > STARS - 2:
                continue
            if any(regcnt[x] >= STARS for x in rr):
                continue
            for c in combo:
                colcnt[c] += 1
            for x in rr:
                regcnt[x] += 1
            nxt = list(remaining)
            for c in range(N):
                nxt[rid[region[r][c]]] -= 1
            rows.append(combo)
            rec(r + 1, nxt)
            rows.pop()
            for c in combo:
                colcnt[c] -= 1
            for x in rr:
                regcnt[x] -= 1

    rec(0, left)
    return out


def read_output(p, bits, extra=40):
    """Run the solution and collect the ASCII the output generator emits."""
    s = [0] * len(p.flops)
    w = [0] * p.nwires
    ns = [0] * len(p.flops)
    scratch = [0] * len(p.flops)
    for _ in range(3):
        s = p.step(s, 0, 0, 0, w, ns)[:]

    def obus():
        p.step(s, 0, 1, 1, w, scratch)   # combinational evaluation only
        return sum(((w[p.wid[p.ports[f'O[{k}]']]]) & 1) << k for k in range(8))

    text, seen = [], None
    for b in list(bits) + [0] * extra:
        s = p.step(s, b, 1, 1, w, ns)[:]
        if p.q(s, SUCCESS_FF):
            o = obus()
            if o != seen and 32 <= o < 127:
                text.append(chr(o))
            seen = o
    return p.q(s, SUCCESS_FF), ''.join(text)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'out/puzzle.json'
    p = Puzzle(path)
    print(f'netlist: {len(p.inst)} cells, {len(p.flops)} flip-flops')

    region = probe_regions(p)
    names = sorted({c for row in region for c in row})
    print(f'\nrecovered {len(names)} regions on an {N}x{N} grid:')
    letters = {n: chr(ord('A') + k) for k, n in enumerate(names)}
    for row in region:
        print('   ' + ' '.join(letters[c] for c in row))

    sols = solve_star_battle(region)
    print(f'\nStar Battle ({STARS} per row / column / region, no touching): '
          f'{len(sols)} solution(s)')
    if not sols:
        raise SystemExit('no solution')
    sol = sols[0]
    for r in range(N):
        print('   ' + ' '.join('*' if c in sol[r] else '.' for c in range(N)))

    bits = [1 if c in sol[r] else 0 for r in range(N) for c in range(N)]
    print('\ninput bits (MSB first, one per cell, row-major):')
    print('   ' + ''.join(map(str, bits)))

    success, text = read_output(p, bits)
    print(f'\nsuccess = {success}')
    print(f'output generator says: {text!r}')
    return 0 if success and text else 1


if __name__ == '__main__':
    sys.exit(main())
