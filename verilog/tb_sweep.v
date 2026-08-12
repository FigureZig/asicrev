// Exhaustive cross-check of the extracted netlist with an independent
// simulator: shift every 8-bit pair (A, B) in and report which ones drive
// `S` high. The list must match what `asicrev sim --target S` found.
`timescale 1ns / 1ps

module tb_sweep;
    reg clk = 0;
    reg rst_n = 0;
    reg en = 1;
    reg a_bit = 0;
    reg b_bit = 0;
    wire s;

    adder_demo dut (
        .A(a_bit),
        .B(b_bit),
        .S(s),
        .clk(clk),
        .en(en),
        .rst_n(rst_n)
    );

    integer av;
    integer bv;
    integer k;
    integer hits;

    task shift_pair(input integer a, input integer b);
        begin
            rst_n = 0;
            #1 clk = 0;
            #1 clk = 1;
            #1 clk = 0;
            rst_n = 1;
            for (k = 7; k >= 0; k = k - 1) begin
                a_bit = (a >> k) & 1;
                b_bit = (b >> k) & 1;
                #1 clk = 0;
                #1 clk = 1;
            end
            #1;
        end
    endtask

    initial begin
        hits = 0;
        for (av = 0; av < 256; av = av + 1) begin
            for (bv = 0; bv < 256; bv = bv + 1) begin
                shift_pair(av, bv);
                if (s === 1'b1) begin
                    hits = hits + 1;
                    $display("SOLUTION A=%0d B=%0d sum=%0d", av, bv, av + bv);
                end
            end
        end
        $display("TOTAL %0d", hits);
        $finish;
    end
endmodule
