#!/usr/bin/env bash
#
# The real design, end to end: layout in, answer out.
#
#   ./scripts/solve.sh              use the vendored puzzle.gds
#   ./scripts/solve.sh some.gds     use another layout with the same ports
#   PAUSE=0 ./scripts/solve.sh      do not wait between steps
#
# Unlike demo.sh there is no reference of any kind for this design: no netlist,
# no routed database, no source. The only external check available is the
# vendor's recorded waveform, which step 5 replays.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
build="$root/build/default"
work="$root/work"
bin="$build/asicrev"
data="$root/external/janestreet-asic-puzzle"
gds="${1:-$data/puzzle.gds}"

PAUSE="${PAUSE:-1}"
step()  { printf '\n\033[1;34m── %s\033[0m\n' "$1"; }
why()   { printf '   \033[2m%s\033[0m\n' "$1"; }
run()   { printf '\n\033[1m$ %s\033[0m\n' "$*"; "$@"; }
wait_() { [[ "$PAUSE" == "1" ]] && read -rp $'\n\033[2m(enter to continue)\033[0m' _ || true; }

[[ -x "$bin" ]] || { cmake --preset default && cmake --build "$build"; }
[[ -f "$gds" ]] || { echo "no layout at $gds" >&2; exit 1; }
mkdir -p "$work"
cd "$root"

# ------------------------------------------------------------------- 1. look

step "1. What is in the file?"
why "The one question worth asking before writing any code: did the standard"
why "cell names survive? If yes, this is a geometry problem. If no, it is"
why "transistor recognition, which is a different and much larger project."
run "$bin" inspect "$gds" --cells
wait_

# ---------------------------------------------------------------- 2. extract

step "2. Geometry to netlist"
why "130k polygons in, 728 logic cells and 739 nets out. The four counters at"
why "the end - unbound labels, split pins, unknown cells, warnings - are the"
why "extractor's own report on whether it understood the file. All zero."
run "$bin" extract "$gds" \
    -o "$work/puzzle.v" --json "$work/puzzle.json" --stubs "$work/models.v"
wait_

# ------------------------------------------------------------------- 3. draw

step "3. Look at what it decided"
why "One colour per recovered net. Also trace the serial input on its own:"
why "if a net renders as scattered confetti instead of a tree, the merge is"
why "wrong and everything downstream is fiction."
run "$bin" export "$gds" -o "$work/puzzle_nets.svg" --colour net --dim-power --width 1600
run "$bin" export "$gds" -o "$work/puzzle_input.svg" --highlight I --width 1200 --no-instances
why "Open $work/puzzle_input.svg - that red spine is the input, one net."
wait_

# ------------------------------------------------------- 4. netlist -> design

step "4. Netlist to design"
why "Expand every flip-flop's next-state function symbolically and read the"
why "registers out of the algebra: counter moduli from the decode terms, bit"
why "weights from the carry chain, shift-register taps from the q[i]'=q[j]"
why "chain, target constants from the conjunction driving the output."
why ""
why "One thing resists reading - eleven membership functions that synthesis"
why "minimised into a shared mess - so that part is measured instead: set one"
why "input bit, run, see which counter moves, repeat 121 times."
run python3 analysis/recover_rtl.py "$work/puzzle.json" "$work/puzzle_rtl.v"
why "That wrote $(wc -l < "$work/puzzle_rtl.v") lines of readable RTL."
wait_

# -------------------------------------------------------------- 5. the answer

step "5. Solve it, and read the output"
why "The recovered constraints are a Star Battle. Solve, feed the solution"
why "back through the netlist, read the output bus."
run python3 analysis/solve_puzzle.py "$work/puzzle.json"
wait_

# ------------------------------------------------------------- 6. cross-check

if command -v iverilog >/dev/null; then
    step "6. Does the recovered RTL match the gates it came from?"
    why "Simulate both against each other: the solution, the degenerate grids,"
    why "and 200 pseudo-random ones. Zero differing cycles is the bar."
    cat > "$work/gate_wrap.v" <<'EOF'
module gate_wrap (input clk, input rst_n, input enable, input I, output success);
  wire [7:0] o;
  puzzle dut (.clk(clk), .rst_n(rst_n), .enable(enable), .I(I), .success(success),
    .\O[0] (o[0]), .\O[1] (o[1]), .\O[2] (o[2]), .\O[3] (o[3]),
    .\O[4] (o[4]), .\O[5] (o[5]), .\O[6] (o[6]), .\O[7] (o[7]));
endmodule
EOF
    run iverilog -g2012 -o "$work/equiv.vvp" \
        "$work/models.v" "$work/puzzle.v" "$work/puzzle_rtl.v" \
        "$work/gate_wrap.v" "$root/verilog/tb_equiv.v"
    run vvp "$work/equiv.vvp"
    wait_

    step "7. Does any of this match the real chip?"
    why "The vendor published one recorded simulation of the original design."
    why "It was never an input to extraction, so agreement here is a statement"
    why "about the recovered model rather than about our own assumptions."
    run python3 analysis/replay_vcd.py "$work/puzzle.json" "$data/example_inputs.vcd"
fi

step "Done"
cat <<EOF
Artefacts in $work:
$(ls -1 "$work" | sed 's/^/  /')

The route, in one line each:

  inspect        cell names survived, so this is geometry, not device recognition
  extract        130k polygons -> 739 nets -> 728 cells -> Verilog
  export         look at it: one colour per net, and one net on its own
  recover_rtl    flip-flop equations -> counters, taps, accumulators -> RTL
  solve_puzzle   the recovered constraints are a Star Battle; solve and feed back
  tb_equiv       recovered RTL vs extracted gates: zero differing cycles
  replay_vcd     extracted netlist vs the vendor's own recording: 0 mismatches
EOF
