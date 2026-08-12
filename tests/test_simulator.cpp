#include "asicrev/netlist/verilog_reader.hpp"
#include "asicrev/sim/simulator.hpp"
#include "asicrev/sim/vcd_writer.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <doctest/doctest.h>

#include <sstream>

using namespace asicrev;

namespace {

/// A 4-bit shift register with an async active-low reset, written the way
/// synthesis would emit it.
const char* kShiftRegister = R"(
module sr (clk, rst_n, si, q0, q1, q2, q3);
 input clk; input rst_n; input si;
 output q0; output q1; output q2; output q3;
 sky130_fd_sc_hd__dfrtp_2 f0 (.CLK(clk), .D(si), .RESET_B(rst_n), .Q(q0));
 sky130_fd_sc_hd__dfrtp_2 f1 (.CLK(clk), .D(q0), .RESET_B(rst_n), .Q(q1));
 sky130_fd_sc_hd__dfrtp_2 f2 (.CLK(clk), .D(q1), .RESET_B(rst_n), .Q(q2));
 sky130_fd_sc_hd__dfrtp_2 f3 (.CLK(clk), .D(q2), .RESET_B(rst_n), .Q(q3));
endmodule
)";

/// Clock arriving through a buffer, to check edge detection is done on the
/// flop's own clock net rather than on the primary input.
const char* kBufferedClock = R"(
module bc (clk, rst_n, d, q);
 input clk; input rst_n; input d; output q;
 wire clk_int;
 sky130_fd_sc_hd__clkbuf_16 cb (.A(clk), .X(clk_int));
 sky130_fd_sc_hd__dfrtp_2 f0 (.CLK(clk_int), .D(d), .RESET_B(rst_n), .Q(q));
endmodule
)";

}  // namespace

TEST_CASE("combinational evaluation is levelised in dependency order") {
    const char* text = R"(
module c (a, b, y);
 input a; input b; output y;
 wire n1; wire n2;
 sky130_fd_sc_hd__nand2_2 g0 (.A(a), .B(b), .Y(n1));
 sky130_fd_sc_hd__clkbuf_16 g1 (.A(n1), .X(n2));
 sky130_fd_sc_hd__clkbuf_16 g2 (.A(n2), .X(y));
endmodule
)";
    const netlist::Netlist nl = netlist::parse_structural_verilog(text, "<c>");
    sim::Simulator s(nl, tech::CellLibrary::sky130_hd());
    CHECK(s.warnings().empty());

    s.set_net("a", sim::Logic::One);
    s.set_net("b", sim::Logic::One);
    s.settle();
    CHECK(s.get_net("y") == sim::Logic::Zero);

    s.set_net("b", sim::Logic::Zero);
    s.settle();
    CHECK(s.get_net("y") == sim::Logic::One);
}

TEST_CASE("async reset clears the flops and the shift register shifts") {
    const netlist::Netlist nl = netlist::parse_structural_verilog(kShiftRegister, "<sr>");
    sim::Simulator s(nl, tech::CellLibrary::sky130_hd());
    CHECK(s.flip_flop_count() == 4);

    s.set_net("rst_n", sim::Logic::Zero);
    s.set_net("si", sim::Logic::One);
    s.settle();
    CHECK(s.get_net("q0") == sim::Logic::Zero);
    CHECK(s.get_net("q3") == sim::Logic::Zero);

    // Reset is asynchronous and level sensitive: clocking does nothing.
    s.clock_edge("clk");
    CHECK(s.get_net("q0") == sim::Logic::Zero);

    s.set_net("rst_n", sim::Logic::One);
    s.settle();
    s.clock_edge("clk");
    CHECK(s.get_net("q0") == sim::Logic::One);
    CHECK(s.get_net("q1") == sim::Logic::Zero);

    s.set_net("si", sim::Logic::Zero);
    s.clock_edge("clk");
    CHECK(s.get_net("q0") == sim::Logic::Zero);
    CHECK(s.get_net("q1") == sim::Logic::One);

    s.clock_edge("clk");
    s.clock_edge("clk");
    CHECK(s.get_net("q3") == sim::Logic::One);
}

TEST_CASE("a buffered clock still triggers its flop") {
    const netlist::Netlist nl = netlist::parse_structural_verilog(kBufferedClock, "<bc>");
    sim::Simulator s(nl, tech::CellLibrary::sky130_hd());

    s.set_net("rst_n", sim::Logic::Zero);
    s.settle();
    s.set_net("rst_n", sim::Logic::One);
    s.set_net("d", sim::Logic::One);
    s.settle();
    CHECK(s.get_net("q") == sim::Logic::Zero);

    s.clock_edge("clk");
    CHECK(s.get_net("q") == sim::Logic::One);
}

TEST_CASE("free inputs exclude driven nets") {
    const netlist::Netlist nl = netlist::parse_structural_verilog(kShiftRegister, "<sr>");
    sim::Simulator s(nl, tech::CellLibrary::sky130_hd());

    std::vector<std::string> names;
    for (std::size_t n : s.free_inputs()) {
        names.push_back(nl.nets[n].name);
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"clk", "rst_n", "si"});
}

TEST_CASE("VCD output records only value changes") {
    std::ostringstream os;
    sim::VcdWriter vcd(os, "top");
    vcd.add_signal("clk");
    vcd.add_signal("d");
    vcd.finish_header();

    vcd.advance_to(0);
    vcd.set("clk", sim::Logic::Zero);
    vcd.set("d", sim::Logic::One);
    vcd.advance_to(1000);
    vcd.set("clk", sim::Logic::One);
    vcd.set("d", sim::Logic::One);  // unchanged, must not be emitted again
    vcd.flush_time();

    const std::string text = os.str();
    CHECK(text.find("$var wire 1 ! clk $end") != std::string::npos);
    CHECK(text.find("#1000") != std::string::npos);
    // 'd' changes once at time 0 and never again.
    const std::size_t first = text.find("1\"");
    REQUIRE(first != std::string::npos);
    CHECK(text.find("1\"", first + 1) == std::string::npos);
}
