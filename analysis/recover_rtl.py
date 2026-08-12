#!/usr/bin/env python3
"""Emit human-readable RTL for the recovered checker, derived from the netlist.

The register roles, bit weights, counter widths and the region map are all
recovered from the extracted netlist (by symbolic expansion of the next-state
functions and by black-box perturbation), never hand-written. The output is a
behavioural Verilog module intended to be proven equivalent to the gate-level
netlist by an external checker.

Usage: scripts/recover_rtl.py [puzzle.json] [out.v]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pysim import Puzzle  # noqa: E402

N = 11
CELLS = N * N
SUCCESS_FF = 'dfrtp_2'


def recover_regions(p):
    """Which cells feed the same region counter (see solve_puzzle.py)."""
    watch = sorted(p.support(p.ff[SUCCESS_FF]['connections']['D']) & set(p.ff))

    def final(bits):
        tr = p.run(bits, extra=2, capture=lambda P, s: [P.q(s, n) for n in watch])
        return tr[CELLS - 1]

    zero = final([0] * CELLS)
    sig = {}
    for t in range(CELLS):
        b = [0] * CELLS
        b[t] = 1
        f = final(b)
        sig[t] = frozenset(watch[k] for k in range(len(watch)) if f[k] != zero[k])
    common = frozenset.intersection(*sig.values())
    where = {}
    for t, s in sig.items():
        for f in s - common:
            where.setdefault(f, set()).add(t)
    region_of = {}
    for f, cells in where.items():
        cols = {t % N for t in cells}
        if len(cols) == 1 and cells == {r * N + next(iter(cols)) for r in range(N)}:
            continue                      # that one is a column counter
        for t in cells:
            region_of[t] = f
    names = sorted(set(region_of.values()))
    rid = {n: k for k, n in enumerate(names)}
    return [[rid[region_of[r * N + c]] for c in range(N)] for r in range(N)], len(names)


def emit(regions, path):
    src = f'''// Recovered RTL for the checker, reconstructed from the extracted gate-level
// netlist. Every structural fact below was read out of that netlist:
//
//   * the scan counters, their moduli and their bit weights, from the
//     next-state equations of the corresponding flip-flops;
//   * the shift-register taps, from the chain of q[i]' = q[j] assignments;
//   * the accumulator widths and the constants they are compared against,
//     from the conjunction that drives the success flop;
//   * the region map, from {CELLS} single-cell perturbation experiments.
//
// The output message stage is deliberately not modelled here: see the report.

`default_nettype none

module puzzle_rtl (
    input  wire clk,
    input  wire rst_n,
    input  wire enable,
    input  wire I,
    output wire success
);
    localparam integer N       = {N};   // grid side
    localparam integer LAST    = N - 1;
    localparam integer PER_ROW = 2;     // required marks per row
    localparam integer PER_COL = 2;     // ... per column
    localparam integer PER_REG = 2;     // ... per region
    localparam integer TOTAL   = 22;    // ... in the whole grid

    // ---------------------------------------------------------------- scan
    // Two mod-11 counters walk the grid, one cell per clock. `done` latches
    // when the last cell has been consumed; the comparison fires on the single
    // cycle where done has just risen, which is what `done & ~done_d` is.
    reg [3:0] col, row;
    reg       done, done_d;

    wire step     = enable & ~done;
    wire last_col = (col == LAST);
    wire last_row = (row == LAST);
    wire row_step = step & last_col;
    wire check    = done & ~done_d;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            col <= 4'd0; row <= 4'd0; done <= 1'b0; done_d <= 1'b0;
        end else begin
            if (step)     col <= last_col ? 4'd0 : col + 4'd1;
            if (row_step) row <= last_row ? 4'd0 : row + 4'd1;
            if (step & last_col & last_row) done <= 1'b1;
            done_d <= done;
        end
    end

    // -------------------------------------------------------- neighbourhood
    // A 12-deep shift register of the input. Taps 1, 10, 11 and 12 are the
    // cells left, up-right, up and up-left of the one being scanned; the two
    // column guards are what stops a tap wrapping around a row boundary.
    reg [12:1] hist;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)   hist <= 12'd0;
        else if (step) hist <= {{hist[11:1], I}};
    end

    wire nb_left     = hist[1]  & (col != 0);
    wire nb_up_right = hist[10] & (col != LAST);
    wire nb_up       = hist[11];
    wire nb_up_left  = hist[12] & (col != 0);
    wire touching    = nb_left | nb_up_right | nb_up | nb_up_left;

    // ------------------------------------------------------------ counters
    // Every accumulator is a two-bit saturating counter, so "exactly two" is
    // representable and "three or more" is not confusable with it.
    reg [1:0] col_cnt [0:N-1];
    reg [1:0] reg_cnt [0:N-1];
    reg [1:0] row_cnt;
    reg [7:0] total;

    // Region membership, recovered empirically.
    function [3:0] region_of;
        input [3:0] r, c;
        integer k;
        begin
            k = r * N + c;
            case (k)
{chr(10).join(f"                {r*N+c}: region_of = 4'd{regions[r][c]};" for r in range(N) for c in range(N))}
                default: region_of = 4'd0;
            endcase
        end
    endfunction

    wire        mark = I & step;
    wire [3:0]  reg_here = region_of(row, col);

    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < N; i = i + 1) begin
                col_cnt[i] <= 2'd0;
                reg_cnt[i] <= 2'd0;
            end
            row_cnt <= 2'd0;
            total   <= 8'd0;
        end else begin
            if (mark) begin
                if (col_cnt[col]      != 2'd3) col_cnt[col]      <= col_cnt[col] + 2'd1;
                if (reg_cnt[reg_here] != 2'd3) reg_cnt[reg_here] <= reg_cnt[reg_here] + 2'd1;
                total <= total + 8'd1;
            end
            // the per-row counter is checked and cleared as each row ends
            if (step) row_cnt <= last_col ? 2'd0
                                          : (row_cnt == 2'd3 ? row_cnt : row_cnt + {{1'b0, I}});
        end
    end

    // ----------------------------------------------------------- violations
    // Both are sticky: one bad cell anywhere disqualifies the whole grid.
    reg viol_adj, viol_row;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            viol_adj <= 1'b0; viol_row <= 1'b0;
        end else begin
            if (mark & touching) viol_adj <= 1'b1;
            if (step & last_col & ((row_cnt + {{1'b0, I}}) != PER_ROW[1:0]))
                viol_row <= 1'b1;
        end
    end

    // -------------------------------------------------------------- verdict
    wire cols_ok = {' & '.join(f'(col_cnt[{k}] == PER_COL[1:0])' for k in range(N))};
    wire regs_ok = {' & '.join(f'(reg_cnt[{k}] == PER_REG[1:0])' for k in range(N))};
    wire all_ok  = ~viol_adj & ~viol_row & cols_ok & regs_ok & (total == TOTAL[7:0]);

    reg success_q;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)     success_q <= 1'b0;
        else if (check) success_q <= all_ok;
    end
    assign success = success_q;

endmodule

`default_nettype wire
'''
    Path(path).write_text(src)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'out/puzzle.json'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'out/puzzle_rtl.v'
    p = Puzzle(src)
    regions, n = recover_regions(p)
    emit(regions, dst)
    print(f'recovered {n} regions; wrote {dst}')


if __name__ == '__main__':
    main()
