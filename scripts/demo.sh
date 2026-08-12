#!/usr/bin/env bash
#
# Guided tour: runs the whole flow on a real layout, one step at a time, and
# says what each step is for. Everything it writes goes to work/.
#
#   ./scripts/demo.sh                 use ../warmup if present
#   ./scripts/demo.sh path/to/gds     use some other design directory
#   PAUSE=0 ./scripts/demo.sh         do not wait between steps
#
# Runs on the warm-up design vendored in external/, which ships with its own
# ground truth, so every check below has something to be checked against.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
build="$root/build/default"
work="$root/work"
bin="$build/asicrev"
design="${1:-$root/external/janestreet-asic-puzzle/warmup}"

PAUSE="${PAUSE:-1}"

step()  { printf '\n\033[1;34m── %s\033[0m\n' "$1"; }
why()   { printf '   \033[2m%s\033[0m\n' "$1"; }
run()   { printf '\n\033[1m$ %s\033[0m\n' "$*"; "$@"; }
wait_() { [[ "$PAUSE" == "1" ]] && read -rp $'\n\033[2m(enter to continue)\033[0m' _ || true; }

# ---------------------------------------------------------------- preflight

if [[ ! -x "$bin" ]]; then
    step "Building first"
    cmake --preset default
    cmake --build "$build"
fi

gds="$design/04_final.gds"
if [[ ! -f "$gds" ]]; then
    cat >&2 <<EOF
Could not find a layout at $gds

Pass a directory that contains 04_final.gds. The one this repository ships
with is external/janestreet-asic-puzzle/warmup.
EOF
    exit 1
fi

mkdir -p "$work"
cd "$root"

# ------------------------------------------------------------------- 1. look

step "1. What is in the file?"
why "Before writing anything: which cells does it place, and on which layers?"
why "If the standard cell names survived, the job is geometry. If not, it is"
why "device recognition, which is a much bigger problem."
run "$bin" inspect "$gds" --layers
wait_

# ---------------------------------------------------------------- 2. extract

step "2. Geometry to netlist"
why "Flatten the hierarchy, cut polygons into rectangles, union-find the"
why "connectivity through vias, bind pin labels to nets, print Verilog."
why "Watch the four zeros at the end: they are the extractor's own report"
why "that it understood every pin of every cell."
run "$bin" extract "$gds" \
    -o "$work/netlist.v" --json "$work/netlist.json" --stubs "$work/models.v"
wait_

# ------------------------------------------------------------------- 3. draw

step "3. Look at what it decided"
why "Same geometry, coloured by the electrical node the extractor assigned."
why "A net that looks like scattered confetti rather than a tree means the"
why "merge is wrong. This is the cheapest sanity check in the project."
run "$bin" export "$gds" -o "$work/nets.svg" --colour net --dim-power --width 1600
run "$bin" export "$gds" -o "$work/layers.svg" --colour layer --max-level 1 --width 1600
why "Open $work/nets.svg and $work/layers.svg in a browser."
wait_

# ------------------------------------------------------------------ 4. check

if [[ -f "$design/01_netlist.v" ]]; then
    step "4. Is it right? (structural)"
    why "The recovered netlist and the reference agree only up to renaming, so"
    why "this is a graph isomorphism on the instance/net graph. Zero backtracks"
    why "means the colour refinement alone separated every node."
    run "$bin" compare "$work/netlist.v" "$design/01_netlist.v" \
        --write-renamed "$work/netlist_named.v"
    wait_
fi

if [[ -f "$design/03_post_place_and_route.def" ]]; then
    step "5. Is it right? (against the routed database)"
    why "The reference database is never an input to extraction - it is only"
    why "ever read back to score the result."
    run "$bin" extract "$gds" -q --physical --power \
        --stubs "$work/models_all.v" \
        --check-def "$design/03_post_place_and_route.def"
    wait_
fi

if command -v yosys >/dev/null && [[ -f "$design/01_netlist.v" ]] \
   && [[ -f "$work/models_all.v" ]]; then
    step "6. Is it right? (formal, by a tool that shares none of our code)"
    why "Names must line up first, which is why step 4 wrote a renamed netlist:"
    why "otherwise the checker can only pair the ports and the proof collapses."
    why "The models come from the run that kept the filler cells, since the"
    why "reference netlist instantiates those too."
    yosys -p "
        read_verilog $work/models_all.v $work/netlist_named.v
        prep -flatten -top adder_demo; async2sync
        design -stash gold
        read_verilog $work/models_all.v $design/01_netlist.v
        prep -flatten -top adder_demo; async2sync
        design -stash gate
        design -copy-from gold -as gold adder_demo
        design -copy-from gate -as gate adder_demo
        equiv_make gold gate equiv
        hierarchy -top equiv
        equiv_simple -seq 4
        equiv_induct
        equiv_status -assert
    " 2>&1 | grep -E 'proven|Equivalence' || true
    wait_
fi

# --------------------------------------------------------------- 7. simulate

step "7. What does it compute?"
why "The netlist is now an experimental subject. Sweep its inputs and see"
why "which ones make the output go high."
if grep -q 'input S;\|output S;' "$work/netlist.v" 2>/dev/null || \
   grep -q '\bS\b' "$work/netlist.v"; then
    run "$bin" sim "$work/netlist.v" \
        --clock clk --reset rst_n --drive en=1 \
        --serial A --serial B --bits 8 --target S --max-solutions 4
    wait_

    step "8. Trace one of them"
    why "A waveform you can open in a viewer, produced by our own simulator."
    run "$bin" sim "$work/netlist.v" \
        --clock clk --reset rst_n --drive en=1 \
        --seq A=11111111 --seq B=11110001 --cycles 10 --vcd "$work/trace.vcd"
else
    why "(this design's ports differ from the warm-up; see README for how to"
    why " drive a different one)"
fi

# ---------------------------------------------- 9. an independent simulator

if command -v iverilog >/dev/null && [[ -f "$work/models_all.v" ]] \
   && grep -q 'module adder_demo' "$work/netlist.v"; then
    step "9. Re-run the same sweep under a simulator we did not write"
    why "Our simulator and our extractor share assumptions. iverilog shares"
    why "none of them, so agreement here is worth more than agreement above."
    run iverilog -g2012 -o "$work/sweep.vvp" \
        "$work/models_all.v" "$work/netlist.v" "$root/verilog/tb_sweep.v"
    run vvp "$work/sweep.vvp"
    wait_
fi

# ------------------------------------------------------------------- summary

step "Done"
echo "Everything written to $work:"
ls -1 "$work"
cat <<'EOF'

What just happened, in one line each:

  inspect   what the file contains, before trusting any of it
  extract   geometry -> nets -> cells -> structural Verilog
  export    the same geometry, coloured by what the extractor decided
  compare   is the result the same circuit as a known-good reference?
  sim       what does the recovered circuit actually compute?

Steps 4, 5, 6 and 9 need reference files or external tools; each is skipped
if what it needs is absent, so the flow runs on any layout.
EOF
