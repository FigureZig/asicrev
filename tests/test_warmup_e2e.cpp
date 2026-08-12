#include "asicrev/def/def_reader.hpp"
#include "asicrev/extract/extractor.hpp"
#include "asicrev/gds/reader.hpp"
#include "asicrev/netlist/compare.hpp"
#include "asicrev/netlist/verilog_reader.hpp"
#include "asicrev/netlist/verilog_writer.hpp"
#include "asicrev/sim/simulator.hpp"
#include "asicrev/tech/sky130.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <set>
#include <string>

using namespace asicrev;

namespace {

std::filesystem::path warmup(const char* name) {
    return std::filesystem::path(ASICREV_WARMUP_DIR) / name;
}

bool warmup_available() {
    return std::filesystem::exists(warmup("04_final.gds"));
}

extract::ExtractResult extract_warmup(bool with_power) {
    const gds::Library lib = gds::read_gds(warmup("04_final.gds"));
    extract::ExtractOptions options;
    options.keep_power_pins = with_power;
    options.keep_physical_cells = with_power;
    return extract_netlist(lib, tech::sky130(), tech::CellLibrary::sky130_hd(), options);
}

/// Shift `a` and `b` in MSB first and report the value of `S`.
bool run_pair(sim::Simulator& s, unsigned a, unsigned b) {
    s.reset_state();
    s.set_net("en", sim::Logic::One);
    s.set_net("rst_n", sim::Logic::Zero);
    s.settle();
    s.clock_edge("clk");
    s.set_net("rst_n", sim::Logic::One);
    s.settle();
    for (unsigned bit = 8; bit-- > 0;) {
        s.set_net("A", ((a >> bit) & 1U) != 0 ? sim::Logic::One : sim::Logic::Zero);
        s.set_net("B", ((b >> bit) & 1U) != 0 ? sim::Logic::One : sim::Logic::Zero);
        s.settle();
        s.clock_edge("clk");
    }
    return s.get_net("S") == sim::Logic::One;
}

}  // namespace

TEST_CASE("warm-up: the layout yields the reference netlist and its function") {
    if (!warmup_available()) {
        MESSAGE("warm-up data not present, skipping");
        return;
    }

    const extract::ExtractResult result = extract_warmup(/*with_power=*/false);
    const extract::ExtractStats& stats = result.stats;

    SUBCASE("extraction is clean") {
        CHECK(result.netlist.module_name == "adder_demo");
        CHECK(stats.unbound_pins == 0);
        CHECK(stats.split_pins == 0);
        CHECK(stats.unknown_cells.empty());
        CHECK(stats.logic_instances == 79);
        CHECK(stats.nets_signal == 84);
    }

    SUBCASE("ports are recovered with the right directions") {
        std::set<std::string> inputs;
        std::set<std::string> outputs;
        for (const netlist::Port& p : result.netlist.ports) {
            if (p.direction == netlist::PortDirection::Input) {
                inputs.insert(p.name);
            } else if (p.direction == netlist::PortDirection::Output) {
                outputs.insert(p.name);
            }
        }
        CHECK(inputs == std::set<std::string>{"A", "B", "clk", "en", "rst_n"});
        CHECK(outputs == std::set<std::string>{"S"});
    }

    SUBCASE("the extracted netlist is isomorphic to the reference") {
        const netlist::Netlist reference = netlist::read_structural_verilog(warmup("01_netlist.v"));
        const netlist::CompareResult cmp =
            netlist::compare_netlists(result.netlist, reference, tech::CellLibrary::sky130_hd());
        INFO("first difference: ",
             cmp.differences.empty() ? std::string{} : cmp.differences.front());
        REQUIRE(cmp.equivalent);
        CHECK(cmp.instance_map.size() == 79);
        CHECK(cmp.net_map.size() == 84);
    }

    SUBCASE("placement and connectivity agree with the reference DEF") {
        const extract::ExtractResult full = extract_warmup(/*with_power=*/true);
        const def::Design design = def::read_def(warmup("03_post_place_and_route.def"));
        CHECK(design.components.size() == 230);
        CHECK(full.netlist.instances.size() == design.components.size());

        std::multiset<std::tuple<std::string, Dbu, Dbu>> placed;
        for (const netlist::Instance& i : full.netlist.instances) {
            placed.emplace(i.cell, i.abutment.xlo, i.abutment.ylo);
        }
        std::size_t matched = 0;
        for (const def::Component& c : design.components) {
            matched += placed.contains({c.cell, c.position.x, c.position.y}) ? std::size_t{1}
                                                                             : std::size_t{0};
        }
        CHECK(matched == design.components.size());
    }

    SUBCASE("simulating the recovered netlist reproduces A + B == 496") {
        sim::Simulator s(result.netlist, tech::CellLibrary::sky130_hd());
        CHECK(s.flip_flop_count() == 16);
        CHECK(s.warnings().empty());

        CHECK(run_pair(s, 255, 241));
        CHECK(run_pair(s, 248, 248));
        CHECK(run_pair(s, 241, 255));
        CHECK_FALSE(run_pair(s, 0, 0));
        CHECK_FALSE(run_pair(s, 255, 240));
        CHECK_FALSE(run_pair(s, 255, 242));

        // The full sweep must find exactly the 15 in-range pairs summing to 496.
        std::size_t hits = 0;
        for (unsigned a = 0; a < 256; ++a) {
            for (unsigned b = 0; b < 256; ++b) {
                if (run_pair(s, a, b)) {
                    ++hits;
                    CHECK(a + b == 496);
                }
            }
        }
        CHECK(hits == 15);
    }
}

TEST_CASE("warm-up: the power-aware extraction matches netlist 02") {
    if (!warmup_available()) {
        return;
    }
    const extract::ExtractResult full = extract_warmup(/*with_power=*/true);
    const netlist::Netlist reference =
        netlist::read_structural_verilog(warmup("02_netlist_with_power_rails.v"));

    netlist::CompareOptions options;
    options.ignore_power_nets = false;
    options.ignore_physical_cells = false;
    const netlist::CompareResult cmp =
        netlist::compare_netlists(full.netlist, reference, tech::CellLibrary::sky130_hd(), options);
    INFO("first difference: ", cmp.differences.empty() ? std::string{} : cmp.differences.front());
    CHECK(cmp.equivalent);
}
