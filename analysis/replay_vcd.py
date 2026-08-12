#!/usr/bin/env python3
"""Replay a recorded waveform through the extracted netlist and compare.

This is the strongest check available on a design with no reference netlist:
the waveform was produced by the original chip and was never an input to
extraction, so agreement is a statement about the recovered model rather than
about our own assumptions.

Two sampling conventions have to be right, and getting either wrong turns a
perfect match into a near-match, which is indistinguishable from a subtle
extraction bug:

  * the stimulus visible at a clock edge is the value from *before* that
    timestamp's updates, not after them;
  * a combinational output must be re-evaluated from the post-edge state, not
    read from the values computed during the edge.

Usage: analysis/replay_vcd.py <netlist.json> <recording.vcd>
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pysim import Puzzle  # noqa: E402


def parse_vcd(path):
    """Return (name -> code, [(time, {name: value})]) for a scalar/vector VCD."""
    text = Path(path).read_text().split('\n')
    codes = {}
    for line in text:
        line = line.strip()
        if line.startswith('$var'):
            parts = line.split()
            codes[parts[3]] = parts[4]
        if line.startswith('$enddefinitions'):
            break

    start = next(i for i, l in enumerate(text) if l.strip().startswith('$enddefinitions'))
    frames, current, time = [], {}, 0
    for line in text[start + 1:]:
        line = line.strip()
        if not line or line.startswith('$'):
            continue
        if line[0] == '#':
            frames.append((time, dict(current)))
            time = int(line[1:])
            continue
        if line[0] == 'b':
            value, code = line[1:].split()
            current[codes.get(code, code)] = value
        elif line[0] in '01xz' and len(line) > 1:
            current[codes.get(line[1:], line[1:])] = line[0]
    frames.append((time, dict(current)))
    return frames


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip().split('\n')[-1])
        return 2
    p = Puzzle(sys.argv[1])
    frames = parse_vcd(sys.argv[2])

    out_nets = [p.ports[f'O[{k}]'] for k in range(8)]
    state = [0] * len(p.flops)
    wires = [0] * p.nwires
    nxt = [0] * len(p.flops)
    scratch = [0] * len(p.flops)

    def output_bus(I, enable, rst_n):
        # evaluate the combinational cloud only, from the current state
        p.step(state, I, enable, rst_n, wires, scratch)
        return sum(((wires[p.wid[n]]) & 1) << k for k, n in enumerate(out_nets))

    previous = 'x'
    checked = mismatched = edges = 0
    text = []
    for i, (_, values) in enumerate(frames):
        clk = values.get('clk', 'x')
        if previous == '0' and clk == '1':
            edges += 1
            before = frames[i - 1][1] if i > 0 else {}
            I = 1 if before.get('I') == '1' else 0
            enable = 1 if before.get('enable') == '1' else 0
            rst_n = 1 if before.get('rst_n') == '1' else 0
            state[:] = p.step(state, I, enable, rst_n, wires, nxt)

            recorded_o = values.get('O')
            recorded_s = values.get('success')
            if recorded_o and set(recorded_o) <= set('01'):
                checked += 1
                ours = output_bus(I, enable, rst_n)
                if int(recorded_o, 2) != ours:
                    mismatched += 1
                elif 32 <= ours < 127:
                    text.append(chr(ours))
            if recorded_s in '01':
                checked += 1
                if str(p.q(state, 'dfrtp_2')) != recorded_s:
                    mismatched += 1
        previous = clk

    print(f'replayed {edges} clock edges from {Path(sys.argv[2]).name}')
    print(f'compared {checked} recorded samples of O and success')
    print(f'mismatches: {mismatched}')
    squashed = ''.join(c for k, c in enumerate(text) if k == 0 or c != text[k - 1])
    if squashed:
        print(f'the chip prints: {squashed!r}')
    return 0 if mismatched == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
