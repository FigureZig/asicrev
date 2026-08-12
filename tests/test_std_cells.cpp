#include "asicrev/netlist/netlist.hpp"
#include "asicrev/sim/simulator.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace asicrev;
using namespace asicrev::tech;

namespace {

/// Build a one-gate netlist and sweep its truth table, so a cell's function is
/// checked through exactly the path the extraction pipeline uses.
std::vector<int> truth_table(const std::string& cell, const std::vector<std::string>& inputs,
                             const std::string& output) {
    const CellLibrary& lib = CellLibrary::sky130_hd();
    const CellModel* model = lib.find(cell);
    REQUIRE_MESSAGE(model != nullptr, "missing cell model: " << cell);

    netlist::Netlist nl;
    nl.module_name = "dut";
    netlist::Instance inst;
    inst.name = "u0";
    inst.cell = cell;
    for (const std::string& i : inputs) {
        inst.pins.push_back(netlist::InstancePin{i, nl.add_net(i)});
    }
    inst.pins.push_back(netlist::InstancePin{output, nl.add_net(output)});
    nl.instances.push_back(inst);

    sim::Simulator simulator(nl, lib);
    std::vector<int> table;
    const std::size_t rows = std::size_t{1} << inputs.size();
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t b = 0; b < inputs.size(); ++b) {
            const bool value = ((row >> (inputs.size() - 1 - b)) & 1U) != 0;
            simulator.set_net(inputs[b], value ? sim::Logic::One : sim::Logic::Zero);
        }
        simulator.settle();
        const sim::Logic v = simulator.get_net(output);
        table.push_back(v == sim::Logic::One ? 1 : (v == sim::Logic::Zero ? 0 : -1));
    }
    return table;
}

}  // namespace

// The expected tables below were derived from the SkyWater PDK gate-level
// models in cells/<base>/sky130_fd_sc_hd__<base>.functional.pp.v, listed with
// the inputs in the order given and counting up in binary.

TEST_CASE("basic gates match the PDK models") {
    CHECK(truth_table("sky130_fd_sc_hd__nand2_2", {"A", "B"}, "Y") == std::vector<int>{1, 1, 1, 0});
    CHECK(truth_table("sky130_fd_sc_hd__and2_2", {"A", "B"}, "X") == std::vector<int>{0, 0, 0, 1});
    CHECK(truth_table("sky130_fd_sc_hd__nor2_2", {"A", "B"}, "Y") == std::vector<int>{1, 0, 0, 0});
    CHECK(truth_table("sky130_fd_sc_hd__or2_2", {"A", "B"}, "X") == std::vector<int>{0, 1, 1, 1});
    CHECK(truth_table("sky130_fd_sc_hd__xor2_2", {"A", "B"}, "X") == std::vector<int>{0, 1, 1, 0});
    CHECK(truth_table("sky130_fd_sc_hd__xnor2_2", {"A", "B"}, "Y") == std::vector<int>{1, 0, 0, 1});
    CHECK(truth_table("sky130_fd_sc_hd__clkbuf_16", {"A"}, "X") == std::vector<int>{0, 1});
    CHECK(truth_table("sky130_fd_sc_hd__inv_2", {"A"}, "Y") == std::vector<int>{1, 0});
}

TEST_CASE("and3 and and4bb match the PDK models") {
    // and3: X = A & B & C
    CHECK(truth_table("sky130_fd_sc_hd__and3_2", {"A", "B", "C"}, "X") ==
          std::vector<int>{0, 0, 0, 0, 0, 0, 0, 1});
    // and4bb: X = ~(A_N | B_N) & C & D, i.e. only A_N=0,B_N=0,C=1,D=1.
    std::vector<int> expected(16, 0);
    expected[0b0011] = 1;
    CHECK(truth_table("sky130_fd_sc_hd__and4bb_2", {"A_N", "B_N", "C", "D"}, "X") == expected);
}

TEST_CASE("and-or compounds match the PDK models") {
    // a21o: X = (A1 & A2) | B1
    CHECK(truth_table("sky130_fd_sc_hd__a21o_2", {"A1", "A2", "B1"}, "X") ==
          std::vector<int>{0, 1, 0, 1, 0, 1, 1, 1});

    // a21bo: X = (A1 & A2) | ~B1_N
    CHECK(truth_table("sky130_fd_sc_hd__a21bo_2", {"A1", "A2", "B1_N"}, "X") ==
          std::vector<int>{1, 0, 1, 0, 1, 0, 1, 1});

    // a21boi: Y = ~((A1 & A2) | ~B1_N)
    CHECK(truth_table("sky130_fd_sc_hd__a21boi_2", {"A1", "A2", "B1_N"}, "Y") ==
          std::vector<int>{0, 1, 0, 1, 0, 1, 0, 0});

    // o21bai: Y = ~((A1 | A2) & ~B1_N)
    CHECK(truth_table("sky130_fd_sc_hd__o21bai_2", {"A1", "A2", "B1_N"}, "Y") ==
          std::vector<int>{1, 1, 0, 1, 0, 1, 0, 1});

    // a31o: X = (A1 & A2 & A3) | B1
    std::vector<int> a31o;
    for (int row = 0; row < 16; ++row) {
        const bool a1 = (row & 0b1000) != 0;
        const bool a2 = (row & 0b0100) != 0;
        const bool a3 = (row & 0b0010) != 0;
        const bool b1 = (row & 0b0001) != 0;
        a31o.push_back(((a1 && a2 && a3) || b1) ? 1 : 0);
    }
    CHECK(truth_table("sky130_fd_sc_hd__a31o_2", {"A1", "A2", "A3", "B1"}, "X") == a31o);
}

TEST_CASE("the cells the real puzzle adds match the PDK models") {
    // or4b: X = A | B | C | ~D_N
    CHECK(truth_table("sky130_fd_sc_hd__or4b_2", {"A", "B", "C", "D_N"}, "X") ==
          std::vector<int>{1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
    // or4bb: X = A | B | ~(C_N & D_N)
    CHECK(truth_table("sky130_fd_sc_hd__or4bb_2", {"A", "B", "C_N", "D_N"}, "X") ==
          std::vector<int>{1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
    // nor3b: Y = C_N & ~(A | B)
    CHECK(truth_table("sky130_fd_sc_hd__nor3b_2", {"A", "B", "C_N"}, "Y") ==
          std::vector<int>{0, 1, 0, 0, 0, 0, 0, 0});
    // nor4b: Y = ~(A | B | C | ~D_N)
    CHECK(truth_table("sky130_fd_sc_hd__nor4b_2", {"A", "B", "C", "D_N"}, "Y") ==
          std::vector<int>{0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    // nand3b: Y = ~(~A_N & B & C)
    CHECK(truth_table("sky130_fd_sc_hd__nand3b_2", {"A_N", "B", "C"}, "Y") ==
          std::vector<int>{1, 1, 1, 0, 1, 1, 1, 1});
    // a41oi: Y = ~((A1 & A2 & A3 & A4) | B1)
    std::vector<int> a41oi;
    for (int row = 0; row < 32; ++row) {
        const bool a = (row & 0b10000) != 0 && (row & 0b01000) != 0 && (row & 0b00100) != 0 &&
                       (row & 0b00010) != 0;
        a41oi.push_back((a || (row & 1) != 0) ? 0 : 1);
    }
    CHECK(truth_table("sky130_fd_sc_hd__a41oi_2", {"A1", "A2", "A3", "A4", "B1"}, "Y") == a41oi);
    // a2111oi: Y = ~((A1 & A2) | B1 | C1 | D1)
    std::vector<int> a2111oi;
    for (int row = 0; row < 32; ++row) {
        const bool a = (row & 0b10000) != 0 && (row & 0b01000) != 0;
        a2111oi.push_back((a || (row & 0b111) != 0) ? 0 : 1);
    }
    CHECK(truth_table("sky130_fd_sc_hd__a2111oi_2", {"A1", "A2", "B1", "C1", "D1"}, "Y") ==
          a2111oi);
    // a221oi: Y = ~((A1 & A2) | (B1 & B2) | C1)
    std::vector<int> a221oi;
    for (int row = 0; row < 32; ++row) {
        const bool a = (row & 0b10000) != 0 && (row & 0b01000) != 0;
        const bool b = (row & 0b00100) != 0 && (row & 0b00010) != 0;
        a221oi.push_back((a || b || (row & 1) != 0) ? 0 : 1);
    }
    CHECK(truth_table("sky130_fd_sc_hd__a221oi_2", {"A1", "A2", "B1", "B2", "C1"}, "Y") == a221oi);
    // o32a / o32ai: X = (A1 | A2 | A3) & (B1 | B2)
    std::vector<int> o32a;
    for (int row = 0; row < 32; ++row) {
        const bool a = (row & 0b11100) != 0;
        const bool b = (row & 0b00011) != 0;
        o32a.push_back((a && b) ? 1 : 0);
    }
    CHECK(truth_table("sky130_fd_sc_hd__o32a_2", {"A1", "A2", "A3", "B1", "B2"}, "X") == o32a);
    std::vector<int> o32ai;
    for (int v : o32a) {
        o32ai.push_back(1 - v);
    }
    CHECK(truth_table("sky130_fd_sc_hd__o32ai_2", {"A1", "A2", "A3", "B1", "B2"}, "Y") == o32ai);
    // o2bb2a: X = ~(A1_N & A2_N) & (B1 | B2)
    std::vector<int> o2bb2a;
    for (int row = 0; row < 16; ++row) {
        const bool a = !((row & 0b1000) != 0 && (row & 0b0100) != 0);
        const bool b = (row & 0b0011) != 0;
        o2bb2a.push_back((a && b) ? 1 : 0);
    }
    CHECK(truth_table("sky130_fd_sc_hd__o2bb2a_2", {"A1_N", "A2_N", "B1", "B2"}, "X") == o2bb2a);
}

TEST_CASE("mux2 selects A1 when S is high") {
    // Inputs ordered A0, A1, S.
    CHECK(truth_table("sky130_fd_sc_hd__mux2_1", {"A0", "A1", "S"}, "X") ==
          std::vector<int>{0, 0, 0, 1, 1, 0, 1, 1});
}

TEST_CASE("drive strength variants share one function") {
    const CellLibrary& lib = CellLibrary::sky130_hd();
    for (const char* name : {"sky130_fd_sc_hd__nand2_1", "sky130_fd_sc_hd__nand2_2",
                             "sky130_fd_sc_hd__nand2_4", "sky130_fd_sc_hd__nand2_8"}) {
        CHECK(lib.find(name) != nullptr);
    }
    CHECK(base_cell_name("sky130_fd_sc_hd__nand2_2") == "nand2");
    CHECK(base_cell_name("sky130_fd_sc_hd__a21boi_4") == "a21boi");
    CHECK(base_cell_name("sky130_fd_sc_hd__tapvpwrvgnd_1") == "tapvpwrvgnd");
}

TEST_CASE("physical cells carry no signal pins") {
    const CellLibrary& lib = CellLibrary::sky130_hd();
    const CellModel* decap = lib.find("sky130_fd_sc_hd__decap_3");
    REQUIRE(decap != nullptr);
    CHECK(decap->is_physical());
    CHECK(decap->input_pins().empty());
    CHECK(decap->output_pins().empty());
}

TEST_CASE("generated Verilog stubs mention every requested cell") {
    const CellLibrary& lib = CellLibrary::sky130_hd();
    const std::string stubs =
        lib.verilog_stubs({"sky130_fd_sc_hd__nand2_2", "sky130_fd_sc_hd__dfrtp_2"});
    CHECK(stubs.find("module sky130_fd_sc_hd__nand2_2") != std::string::npos);
    CHECK(stubs.find("module sky130_fd_sc_hd__dfrtp_2") != std::string::npos);
    CHECK(stubs.find("posedge CLK") != std::string::npos);
    CHECK(stubs.find("negedge RESET_B") != std::string::npos);
}
