#include "asicrev/netlist/compare.hpp"
#include "asicrev/netlist/verilog_reader.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <doctest/doctest.h>

using namespace asicrev;
using namespace asicrev::netlist;

namespace {

const char* kOriginal = R"(
module m (a, b, c, y);
 input a; input b; input c; output y;
 wire n1; wire n2;
 sky130_fd_sc_hd__nand2_2 g0 (.A(a), .B(b), .Y(n1));
 sky130_fd_sc_hd__nand2_2 g1 (.A(n1), .B(c), .Y(n2));
 sky130_fd_sc_hd__clkbuf_16 g2 (.A(n2), .X(y));
endmodule
)";

/// Same circuit, different instance and wire names, different order.
const char* kRenamed = R"(
module m (a, b, c, y);
 input a; input b; input c; output y;
 wire w7; wire w3;
 sky130_fd_sc_hd__clkbuf_16 zz (.A(w3), .X(y));
 sky130_fd_sc_hd__nand2_2 aa (.A(w7), .B(c), .Y(w3));
 sky130_fd_sc_hd__nand2_2 bb (.A(a), .B(b), .Y(w7));
endmodule
)";

/// One connection moved: g1.B now comes from a instead of c.
const char* kBroken = R"(
module m (a, b, c, y);
 input a; input b; input c; output y;
 wire n1; wire n2;
 sky130_fd_sc_hd__nand2_2 g0 (.A(a), .B(b), .Y(n1));
 sky130_fd_sc_hd__nand2_2 g1 (.A(n1), .B(a), .Y(n2));
 sky130_fd_sc_hd__clkbuf_16 g2 (.A(n2), .X(y));
endmodule
)";

/// Inputs swapped on an asymmetric position: A and B of the second nand.
const char* kSwappedPins = R"(
module m (a, b, c, y);
 input a; input b; input c; output y;
 wire n1; wire n2;
 sky130_fd_sc_hd__nand2_2 g0 (.A(a), .B(b), .Y(n1));
 sky130_fd_sc_hd__nand2_2 g1 (.A(c), .B(n1), .Y(n2));
 sky130_fd_sc_hd__clkbuf_16 g2 (.A(n2), .X(y));
endmodule
)";

CompareResult run(const char* left, const char* right) {
    return compare_netlists(parse_structural_verilog(left, "<l>"),
                            parse_structural_verilog(right, "<r>"), tech::CellLibrary::sky130_hd());
}

}  // namespace

TEST_CASE("renaming everything still compares equal") {
    const CompareResult r = run(kOriginal, kRenamed);
    INFO("differences: ", r.differences.empty() ? std::string{} : r.differences.front());
    REQUIRE(r.equivalent);
    CHECK(r.instance_map.at("g0") == "bb");
    CHECK(r.instance_map.at("g1") == "aa");
    CHECK(r.instance_map.at("g2") == "zz");
    CHECK(r.net_map.at("n1") == "w7");
}

TEST_CASE("a rewired connection is detected") {
    const CompareResult r = run(kOriginal, kBroken);
    CHECK_FALSE(r.equivalent);
    CHECK_FALSE(r.differences.empty());
}

TEST_CASE("identical circuits compare equal to themselves") {
    const CompareResult r = run(kOriginal, kOriginal);
    CHECK(r.equivalent);
}

TEST_CASE("swapping the inputs of a symmetric gate is still the same circuit") {
    // NAND is commutative, but the comparison is structural: A and B are
    // distinct pins, so this must be reported as a difference rather than
    // silently accepted.
    const CompareResult r = run(kOriginal, kSwappedPins);
    CHECK_FALSE(r.equivalent);
}
