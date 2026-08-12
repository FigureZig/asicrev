#include "asicrev/netlist/verilog_reader.hpp"
#include "asicrev/netlist/verilog_writer.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <doctest/doctest.h>

using namespace asicrev;
using namespace asicrev::netlist;

namespace {

const char* kSample = R"(
/* a synthesised netlist, yosys style */
module adder_demo (A,
    B,
    S,
    clk);
 input A;
 input B;
 output S;
 input clk;

 wire \a_reg[0] ;
 wire \sr_a/_00_ ;

 sky130_fd_sc_hd__nand2_2 \add0/_32_  (.A(A),
    .B(B),
    .Y(\sr_a/_00_ ));
 sky130_fd_sc_hd__dfrtp_2 \sr_a/reg  (.CLK(clk),
    .D(\sr_a/_00_ ),
    .RESET_B(A),
    .Q(\a_reg[0] ));
 sky130_fd_sc_hd__clkbuf_16 buf0 (.A(\a_reg[0] ),
    .X(S));
endmodule
)";

}  // namespace

TEST_CASE("structural Verilog reader understands escaped identifiers") {
    const Netlist nl = parse_structural_verilog(kSample, "<sample>");

    CHECK(nl.module_name == "adder_demo");
    CHECK(nl.instances.size() == 3);
    CHECK(nl.ports.size() == 4);

    const Instance& nand = nl.instances[0];
    CHECK(nand.cell == "sky130_fd_sc_hd__nand2_2");
    CHECK(nand.name == "add0/_32_");
    REQUIRE(nand.find("Y") != nullptr);
    CHECK(nl.nets[nand.find("Y")->net].name == "sr_a/_00_");

    const std::size_t a_reg = nl.net_by_name("a_reg[0]");
    REQUIRE(a_reg != kNoNet);

    // Port directions come from the declarations, not the header order.
    bool saw_output = false;
    for (const Port& p : nl.ports) {
        if (p.name == "S") {
            CHECK(p.direction == PortDirection::Output);
            saw_output = true;
        }
    }
    CHECK(saw_output);
}

TEST_CASE("writing then reading a netlist preserves its structure") {
    const Netlist original = parse_structural_verilog(kSample, "<sample>");
    const std::string text = to_verilog(original, tech::CellLibrary::sky130_hd());
    const Netlist again = parse_structural_verilog(text, "<roundtrip>");

    CHECK(again.module_name == original.module_name);
    CHECK(again.instances.size() == original.instances.size());
    CHECK(again.ports.size() == original.ports.size());

    for (std::size_t i = 0; i < original.instances.size(); ++i) {
        CHECK(again.instances[i].cell == original.instances[i].cell);
        CHECK(again.instances[i].pins.size() == original.instances[i].pins.size());
    }
}

TEST_CASE("identifiers are escaped only when they have to be") {
    CHECK(escape_identifier("clk") == "clk");
    CHECK(escape_identifier("n123") == "n123");
    CHECK(escape_identifier("a_reg[0]") == "\\a_reg[0] ");
    CHECK(escape_identifier("add0/_32_") == "\\add0/_32_ ");
}
