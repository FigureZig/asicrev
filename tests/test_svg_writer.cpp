#include "asicrev/export/svg_writer.hpp"
#include "asicrev/extract/extractor.hpp"
#include "asicrev/gds/reader.hpp"
#include "asicrev/tech/sky130.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>

using namespace asicrev;

namespace {

std::size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

extract::ExtractResult extract_warmup_geometry() {
    const gds::Library lib =
        gds::read_gds(std::filesystem::path(ASICREV_WARMUP_DIR) / "04_final.gds");
    extract::ExtractOptions options;
    options.keep_geometry = true;
    options.keep_power_pins = true;
    options.keep_physical_cells = true;
    return extract::extract_netlist(lib, tech::sky130(), tech::CellLibrary::sky130_hd(), options);
}

}  // namespace

TEST_CASE("geometry is only produced when asked for") {
    if (!std::filesystem::exists(std::filesystem::path(ASICREV_WARMUP_DIR) / "04_final.gds")) {
        return;
    }
    const gds::Library lib =
        gds::read_gds(std::filesystem::path(ASICREV_WARMUP_DIR) / "04_final.gds");
    const extract::ExtractResult without =
        extract::extract_netlist(lib, tech::sky130(), tech::CellLibrary::sky130_hd(), {});
    CHECK(without.geometry.empty());

    const extract::ExtractResult with = extract_warmup_geometry();
    CHECK(with.geometry.size() == with.stats.rectangles);
    CHECK(with.instance_boxes.size() == with.netlist.instances.size());
}

TEST_CASE("every exported rectangle carries the net the netlist agrees with") {
    if (!std::filesystem::exists(std::filesystem::path(ASICREV_WARMUP_DIR) / "04_final.gds")) {
        return;
    }
    const extract::ExtractResult r = extract_warmup_geometry();

    // Rectangles are tagged with an index into the netlist's nets, and every
    // net that any pin uses must appear on at least one rectangle: otherwise
    // the picture and the netlist would be telling different stories.
    std::vector<bool> seen(r.netlist.nets.size(), false);
    for (const extract::ExtractedRect& rect : r.geometry) {
        CHECK(rect.conductor < tech::sky130().conductors.size());
        if (rect.net != netlist::kNoNet) {
            REQUIRE(rect.net < r.netlist.nets.size());
            seen[rect.net] = true;
        }
    }
    for (const netlist::Instance& inst : r.netlist.instances) {
        for (const netlist::InstancePin& p : inst.pins) {
            if (p.net != netlist::kNoNet) {
                CHECK(seen[p.net]);
            }
        }
    }
}

TEST_CASE("SVG output is well formed and honours the clip window") {
    if (!std::filesystem::exists(std::filesystem::path(ASICREV_WARMUP_DIR) / "04_final.gds")) {
        return;
    }
    const extract::ExtractResult r = extract_warmup_geometry();

    std::ostringstream full;
    svg::write_svg(full, r, tech::sky130(), {});
    const std::string text = full.str();
    CHECK(text.rfind("<svg", 0) == 0);
    CHECK(text.find("</svg>") != std::string::npos);
    // One group per conductor level that carries geometry.
    CHECK(count_occurrences(text, "<g id=\"") >= 4);
    CHECK(count_occurrences(text, "<g id=\"li1\">") == 1);
    CHECK(count_occurrences(text, "<rect") > 1000);

    svg::SvgOptions clipped;
    clipped.clip = Rect{20000, 20000, 30000, 30000};
    std::ostringstream small;
    svg::write_svg(small, r, tech::sky130(), clipped);
    CHECK(count_occurrences(small.str(), "<rect") < count_occurrences(text, "<rect"));
}

TEST_CASE("highlighting a net colours that net and nothing else") {
    if (!std::filesystem::exists(std::filesystem::path(ASICREV_WARMUP_DIR) / "04_final.gds")) {
        return;
    }
    const extract::ExtractResult r = extract_warmup_geometry();
    const std::size_t clk = r.netlist.net_by_name("clk");
    REQUIRE(clk != netlist::kNoNet);

    svg::SvgOptions options;
    options.highlight_net = clk;
    std::ostringstream os;
    svg::write_svg(os, r, tech::sky130(), options);
    const std::string text = os.str();

    const std::size_t highlighted = count_occurrences(text, "fill=\"#d62828\"");
    const std::size_t expected = static_cast<std::size_t>(
        std::count_if(r.geometry.begin(), r.geometry.end(),
                      [&](const extract::ExtractedRect& g) { return g.net == clk; }));
    CHECK(highlighted == expected);
    CHECK(highlighted > 0);
}
