"""Compile the extracted puzzle netlist into a straight-line Python simulator.

Reads puzzle.json (produced by `asicrev extract --json`), topologically
sorts the combinational gates and emits one Python expression per net, so a
cycle is a single function call. Fast enough to sweep thousands of inputs.
"""
import json
import re
from collections import defaultdict

OUTP = {'X', 'Y', 'Q', 'Q_N', 'HI', 'LO', 'Z'}


def base(cell):
    return re.sub(r'_\d+$', '', cell.split('__')[1])


def AND(*a):
    return '(' + '&'.join(a) + ')'


def OR(*a):
    return '(' + '|'.join(a) + ')'


def NOT(a):
    return '(1-' + a + ')'


F = {
    'buf': lambda p: p['A'], 'clkbuf': lambda p: p['A'],
    'inv': lambda p: NOT(p['A']), 'clkinv': lambda p: NOT(p['A']),
    'and2': lambda p: AND(p['A'], p['B']),
    'and3': lambda p: AND(p['A'], p['B'], p['C']),
    'and4': lambda p: AND(p['A'], p['B'], p['C'], p['D']),
    'and2b': lambda p: AND(NOT(p['A_N']), p['B']),
    'and3b': lambda p: AND(NOT(p['A_N']), p['B'], p['C']),
    'and4b': lambda p: AND(NOT(p['A_N']), p['B'], p['C'], p['D']),
    'and4bb': lambda p: AND(NOT(p['A_N']), NOT(p['B_N']), p['C'], p['D']),
    'or2': lambda p: OR(p['A'], p['B']),
    'or3': lambda p: OR(p['A'], p['B'], p['C']),
    'or4': lambda p: OR(p['A'], p['B'], p['C'], p['D']),
    'or3b': lambda p: OR(p['A'], p['B'], NOT(p['C_N'])),
    'or4b': lambda p: OR(p['A'], p['B'], p['C'], NOT(p['D_N'])),
    'or4bb': lambda p: OR(p['A'], p['B'], NOT(AND(p['C_N'], p['D_N']))),
    'nand2': lambda p: NOT(AND(p['A'], p['B'])),
    'nand3': lambda p: NOT(AND(p['A'], p['B'], p['C'])),
    'nand4': lambda p: NOT(AND(p['A'], p['B'], p['C'], p['D'])),
    'nand2b': lambda p: NOT(AND(NOT(p['A_N']), p['B'])),
    'nand3b': lambda p: NOT(AND(NOT(p['A_N']), p['B'], p['C'])),
    'nor2': lambda p: NOT(OR(p['A'], p['B'])),
    'nor3': lambda p: NOT(OR(p['A'], p['B'], p['C'])),
    'nor4': lambda p: NOT(OR(p['A'], p['B'], p['C'], p['D'])),
    'nor3b': lambda p: NOT(OR(p['A'], p['B'], NOT(p['C_N']))),
    'nor4b': lambda p: NOT(OR(p['A'], p['B'], p['C'], NOT(p['D_N']))),
    'xor2': lambda p: '(' + p['A'] + '^' + p['B'] + ')',
    'xnor2': lambda p: NOT('(' + p['A'] + '^' + p['B'] + ')'),
    'mux2': lambda p: f"({p['A1']} if {p['S']} else {p['A0']})",
    'a21o': lambda p: OR(AND(p['A1'], p['A2']), p['B1']),
    'a21oi': lambda p: NOT(OR(AND(p['A1'], p['A2']), p['B1'])),
    'a21bo': lambda p: OR(AND(p['A1'], p['A2']), NOT(p['B1_N'])),
    'a21boi': lambda p: NOT(OR(AND(p['A1'], p['A2']), NOT(p['B1_N']))),
    'a22o': lambda p: OR(AND(p['A1'], p['A2']), AND(p['B1'], p['B2'])),
    'a22oi': lambda p: NOT(OR(AND(p['A1'], p['A2']), AND(p['B1'], p['B2']))),
    'a31o': lambda p: OR(AND(p['A1'], p['A2'], p['A3']), p['B1']),
    'a31oi': lambda p: NOT(OR(AND(p['A1'], p['A2'], p['A3']), p['B1'])),
    'a32o': lambda p: OR(AND(p['A1'], p['A2'], p['A3']), AND(p['B1'], p['B2'])),
    'a41oi': lambda p: NOT(OR(AND(p['A1'], p['A2'], p['A3'], p['A4']), p['B1'])),
    'a211o': lambda p: OR(AND(p['A1'], p['A2']), p['B1'], p['C1']),
    'a211oi': lambda p: NOT(OR(AND(p['A1'], p['A2']), p['B1'], p['C1'])),
    'a221o': lambda p: OR(AND(p['A1'], p['A2']), AND(p['B1'], p['B2']), p['C1']),
    'a221oi': lambda p: NOT(OR(AND(p['A1'], p['A2']), AND(p['B1'], p['B2']), p['C1'])),
    'a311o': lambda p: OR(AND(p['A1'], p['A2'], p['A3']), p['B1'], p['C1']),
    'a2111oi': lambda p: NOT(OR(AND(p['A1'], p['A2']), p['B1'], p['C1'], p['D1'])),
    'o21a': lambda p: AND(OR(p['A1'], p['A2']), p['B1']),
    'o21ai': lambda p: NOT(AND(OR(p['A1'], p['A2']), p['B1'])),
    'o21ba': lambda p: AND(OR(p['A1'], p['A2']), NOT(p['B1_N'])),
    'o21bai': lambda p: NOT(AND(OR(p['A1'], p['A2']), NOT(p['B1_N']))),
    'o22a': lambda p: AND(OR(p['A1'], p['A2']), OR(p['B1'], p['B2'])),
    'o22ai': lambda p: NOT(AND(OR(p['A1'], p['A2']), OR(p['B1'], p['B2']))),
    'o31a': lambda p: AND(OR(p['A1'], p['A2'], p['A3']), p['B1']),
    'o31ai': lambda p: NOT(AND(OR(p['A1'], p['A2'], p['A3']), p['B1'])),
    'o32a': lambda p: AND(OR(p['A1'], p['A2'], p['A3']), OR(p['B1'], p['B2'])),
    'o32ai': lambda p: NOT(AND(OR(p['A1'], p['A2'], p['A3']), OR(p['B1'], p['B2']))),
    'o211a': lambda p: AND(OR(p['A1'], p['A2']), p['B1'], p['C1']),
    'o211ai': lambda p: NOT(AND(OR(p['A1'], p['A2']), p['B1'], p['C1'])),
    'o221a': lambda p: AND(OR(p['A1'], p['A2']), OR(p['B1'], p['B2']), p['C1']),
    'o311a': lambda p: AND(OR(p['A1'], p['A2'], p['A3']), p['B1'], p['C1']),
    'o2bb2a': lambda p: AND(NOT(AND(p['A1_N'], p['A2_N'])), OR(p['B1'], p['B2'])),
}


class Puzzle:
    def __init__(self, path='puzzle.json'):
        nl = json.load(open(path))['netlist']
        self.nl = nl
        self.inst = {i['name']: i for i in nl['instances']}
        self.ports = {p['name']: p['net'] for p in nl['ports']}
        self.ff = {n: i for n, i in self.inst.items() if 'df' in i['cell']}
        self.qnet = {i['connections']['Q']: n for n, i in self.ff.items()}
        self.driver = {}
        for i in nl['instances']:
            for pin, net in i['connections'].items():
                if net and pin in OUTP and 'df' not in i['cell']:
                    self.driver[net] = (i, pin)
        self.flops = sorted(self.ff)
        self.fidx = {n: k for k, n in enumerate(self.flops)}
        self._compile()

    def _sym(self, net):
        if net in self.qnet:
            return f's[{self.fidx[self.qnet[net]]}]'
        if net in ('I', 'enable', 'rst_n'):
            return net
        return f'w[{self.wid.setdefault(net, len(self.wid))}]'

    def _compile(self):
        self.wid = {}
        # topological order over combinational gates
        order, state = [], {}

        def visit(net):
            if net in self.qnet or net in ('I', 'enable', 'rst_n'):
                return
            st = state.get(net)
            if st == 2:
                return
            if st == 1:
                raise RuntimeError(f'combinational loop at {net}')
            state[net] = 1
            d = self.driver.get(net)
            if d is not None:
                inst, _ = d
                for pin, n in inst['connections'].items():
                    if pin not in OUTP and n:
                        visit(n)
                order.append(net)
            state[net] = 2

        for net in list(self.driver):
            visit(net)

        lines = []
        for net in order:
            inst, pin = self.driver[net]
            b = base(inst['cell'])
            if b == 'conb':
                lines.append(f'{self._sym(net)} = ' + ('1' if pin == 'HI' else '0'))
                continue
            if b in ('decap', 'tapvpwrvgnd', 'fill', 'diode'):
                continue
            p = {k: self._sym(v) for k, v in inst['connections'].items()
                 if k not in OUTP and v}
            lines.append(f'{self._sym(net)} = {F[b](p)}')

        # next-state expressions
        nxt = []
        for n in self.flops:
            i = self.ff[n]
            d = i['connections'].get('D')
            e = self._sym(d) if d else '0'
            k = self.fidx[n]
            if 'RESET_B' in i['connections']:
                nxt.append(f'ns[{k}] = ({e}) if rst_n else 0')
            elif 'SET_B' in i['connections']:
                nxt.append(f'ns[{k}] = ({e}) if rst_n else 1')
            else:
                nxt.append(f'ns[{k}] = {e}')

        src = ('def step(s, I, enable, rst_n, w, ns):\n    '
               + '\n    '.join(lines + nxt) + '\n    return ns\n')
        self.nwires = len(self.wid)
        g = {}
        exec(compile(src, '<netlist>', 'exec'), g)
        self.step = g['step']

    def run(self, bits, extra=4, reset_cycles=3, capture=None):
        """Reset, then feed `bits` one per cycle with enable=1. Returns traces."""
        s = [0] * len(self.flops)
        w = [0] * self.nwires
        ns = [0] * len(self.flops)
        for _ in range(reset_cycles):
            s = self.step(s, 0, 0, 0, w, ns)[:]
        trace = []
        for b in list(bits) + [0] * extra:
            s = self.step(s, b, 1, 1, w, ns)[:]
            if capture is not None:
                trace.append(capture(self, s))
        self.state = s
        return trace

    def q(self, s, name):
        return s[self.fidx[name]]

    def support(self, net):
        """Flip-flops and primary inputs a net combinationally depends on."""
        seen, stack, sup = set(), [net], set()
        while stack:
            n = stack.pop()
            if not n or n in seen:
                continue
            seen.add(n)
            if n in self.qnet:
                sup.add(self.qnet[n])
                continue
            if n in ('I', 'enable', 'rst_n'):
                sup.add(n)
                continue
            d = self.driver.get(n)
            if d is None:
                continue
            for pin, m in d[0]['connections'].items():
                if pin not in OUTP and m:
                    stack.append(m)
        return sup
