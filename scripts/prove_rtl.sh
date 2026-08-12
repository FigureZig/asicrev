#!/usr/bin/env bash
#
# Prove that the recovered RTL and the extracted gate netlist are the same
# circuit, by bounded model checking a miter between them.
#
#   ./scripts/prove_rtl.sh            depth 128  — a complete proof, ~5.5 h
#   ./scripts/prove_rtl.sh 45         depth 45   — four rows of the scan, ~1 min
#   ./scripts/prove_rtl.sh 20         depth 20   — smoke test, seconds
#
# Why 128 is complete rather than merely deep: the design is a straight-line
# scan of 121 cells that latches `done` on the last one and then ignores its
# input forever. Any behaviour it has is reached within 121 cycles of reset, so
# a bound past that covers the whole reachable state space. Below 121 the result
# is strong evidence and nothing more.
#
# Reference run: depth 128, 1 209 641 variables, 3 207 542 clauses,
#                332 minutes, no counterexample.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
build="$root/build/default"
work="$root/work"
bin="$build/asicrev"
data="$root/external/janestreet-asic-puzzle"
depth="${1:-128}"

command -v yosys >/dev/null || { echo "yosys not installed: ./scripts/bootstrap.sh --all" >&2; exit 1; }
[[ -x "$bin" ]] || { cmake --preset default && cmake --build "$build"; }
mkdir -p "$work"
cd "$root"

echo "== Regenerating both sides from the layout"
"$bin" extract "$data/puzzle.gds" -q \
    -o "$work/puzzle.v" --json "$work/puzzle.json" --stubs "$work/models.v"
python3 analysis/recover_rtl.py "$work/puzzle.json" "$work/puzzle_rtl.v"

# Only `success` is compared: the message stage is 23 flip-flops driving an
# output bus through a feedback sequence, and is characterised by simulation
# rather than reconstructed. See the report.
cat > "$work/gate_wrap.v" <<'EOF'
module gate_wrap (input clk, input rst_n, input enable, input I, output success);
  wire [7:0] o;
  puzzle dut (.clk(clk), .rst_n(rst_n), .enable(enable), .I(I), .success(success),
    .\O[0] (o[0]), .\O[1] (o[1]), .\O[2] (o[2]), .\O[3] (o[3]),
    .\O[4] (o[4]), .\O[5] (o[5]), .\O[6] (o[6]), .\O[7] (o[7]));
endmodule
EOF

echo "== Bounded model check to depth $depth"
[[ "$depth" -ge 121 ]] \
    && echo "   (>= 121: complete for every reachable behaviour; expect hours)" \
    || echo "   (< 121: covers the first $depth cycles only; evidence, not proof)"

yosys -p "
    read_verilog $work/models.v $work/puzzle.v $work/gate_wrap.v
    prep -flatten -top gate_wrap; memory_map; async2sync; opt -full
    design -stash gate
    read_verilog -sv $work/puzzle_rtl.v
    prep -flatten -top puzzle_rtl; memory_map; async2sync; opt -full
    design -stash gold
    design -copy-from gold -as gold puzzle_rtl
    design -copy-from gate -as gate gate_wrap
    miter -equiv -flatten -make_assert gold gate miter
    hierarchy -top miter; opt -full
    sat -seq $depth -prove-asserts -set-init-zero -verify
" 2>&1 | grep -E 'Solving problem|SAT proof|Assert|FAIL'
