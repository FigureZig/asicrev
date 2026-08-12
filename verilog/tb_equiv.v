// Compare the recovered RTL against the gate-level netlist it was derived from.
//
// Both are driven with the same stimulus and their `success` outputs are
// compared every cycle. The inputs are the two degenerate grids plus a long
// pseudo-random sweep; the solution itself is not needed here, and is checked
// separately by analysis/solve_puzzle.py, which runs it through the extracted
// netlist directly.
//
// Expects: puzzle_rtl.v, the extracted netlist, its cell models, and a wrapper
// exposing only `success` from the gate-level design.

`timescale 1ns / 1ps

module tb_equiv;
    localparam integer CELLS  = 121;   // one input bit per grid cell
    localparam integer EXTRA  = 9;     // a few cycles past the last one
    localparam integer TRIALS = 200;

    reg clk = 0;
    reg rst_n = 0;
    reg enable = 0;
    reg I = 0;
    wire s_rtl;
    wire s_gate;

    puzzle_rtl rtl  (.clk(clk), .rst_n(rst_n), .enable(enable), .I(I), .success(s_rtl));
    gate_wrap  gate (.clk(clk), .rst_n(rst_n), .enable(enable), .I(I), .success(s_gate));

    integer k;
    integer trial;
    integer mismatches;
    integer high_rtl;
    reg [CELLS-1:0] pattern;
    reg [CELLS-1:0] lfsr;

    // One run: reset, shift `p` in most-significant bit first, compare every
    // cycle. The clock is driven high then low so that the sampled value is
    // unambiguous; driving it the other way round makes the first edge a no-op
    // and every later sample lands on the wrong side of the edge.
    task run_pattern(input [CELLS-1:0] p);
        begin
            rst_n = 0;
            enable = 0;
            for (k = 0; k < 3; k = k + 1) begin
                #1 clk = 1;
                #1 clk = 0;
            end
            rst_n = 1;
            enable = 1;
            for (k = 0; k < CELLS + EXTRA; k = k + 1) begin
                I = (k < CELLS) ? p[CELLS - 1 - k] : 1'b0;
                #1 clk = 1;
                #1 clk = 0;
                if (s_rtl !== s_gate) mismatches = mismatches + 1;
            end
            if (s_rtl === 1'b1) high_rtl = high_rtl + 1;
        end
    endtask

    initial begin
        mismatches = 0;
        high_rtl = 0;

        run_pattern({CELLS{1'b0}});
        $display("empty grid : rtl=%b gate=%b", s_rtl, s_gate);

        run_pattern({CELLS{1'b1}});
        $display("full grid  : rtl=%b gate=%b", s_rtl, s_gate);

        lfsr = 121'h1;
        for (trial = 0; trial < TRIALS; trial = trial + 1) begin
            for (k = 0; k < CELLS; k = k + 1)
                lfsr = {lfsr[CELLS-2:0], lfsr[CELLS-1] ^ lfsr[112] ^ lfsr[100] ^ lfsr[3]};
            run_pattern(lfsr);
        end

        $display("%0d runs x %0d cycles compared", TRIALS + 2, CELLS + EXTRA);
        $display("differing cycles between recovered RTL and extracted gates: %0d",
                 mismatches);
        if (mismatches != 0) $display("MISMATCH");
        else                 $display("recovered RTL and extracted gates agree everywhere");
        $finish;
    end
endmodule
