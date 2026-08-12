#include "asicrev/export/svg_writer.hpp"
#include "asicrev/extract/extractor.hpp"
#include "asicrev/gds/reader.hpp"
#include "asicrev/tech/sky130.hpp"
#include "asicrev/tech/std_cells.hpp"

#include "commands.hpp"

#include <fmt/format.h>

#include <fstream>
#include <vector>

namespace asicrev::app {

namespace {

struct ExportCliOptions {
    std::string gds;
    std::string top;
    std::string out_svg;
    std::string colour = "net";
    int width = 1600;
    std::vector<Dbu> clip;  // x0 y0 x1 y1 in database units
    bool no_instances = false;
    bool physical = false;
    int max_level = 5;
    bool dim_power = false;
    std::string highlight;
};

void run_export(const ExportCliOptions& opt) {
    const gds::Library lib = gds::read_gds(opt.gds);

    extract::ExtractOptions eo;
    eo.top_cell = opt.top;
    eo.keep_geometry = true;
    eo.keep_power_pins = true;
    eo.keep_physical_cells = opt.physical;

    const extract::ExtractResult result =
        extract_netlist(lib, tech::sky130(), tech::CellLibrary::sky130_hd(), eo);

    svg::SvgOptions so;
    so.colour_by = opt.colour == "layer" ? svg::ColourBy::Layer : svg::ColourBy::Net;
    so.width_px = opt.width;
    so.draw_instances = !opt.no_instances;
    so.dim_power = opt.dim_power;
    so.max_level = static_cast<std::size_t>(opt.max_level);
    if (!opt.highlight.empty()) {
        so.highlight_net = result.netlist.net_by_name(opt.highlight);
        if (so.highlight_net == netlist::kNoNet) {
            fmt::print(stderr, "warning: net '{}' not found\n", opt.highlight);
        }
    }
    if (opt.clip.size() == 4) {
        so.clip = Rect::from_points(opt.clip[0], opt.clip[1], opt.clip[2], opt.clip[3]);
    }

    std::ofstream out(opt.out_svg);
    svg::write_svg(out, result, tech::sky130(), so);
    fmt::print("wrote {} ({} rectangles, {} nets, coloured by {})\n", opt.out_svg,
               result.geometry.size(), result.netlist.nets.size(), opt.colour);
}

}  // namespace

void register_export(CLI::App& root) {
    auto opt = std::make_shared<ExportCliOptions>();
    CLI::App* cmd = root.add_subcommand(
        "export", "Render the extracted geometry as SVG, coloured by recovered net");
    cmd->add_option("gds", opt->gds, "GDSII stream file")->required()->check(CLI::ExistingFile);
    cmd->add_option("-o,--svg", opt->out_svg, "Output SVG file")->required();
    cmd->add_option("--top", opt->top, "Top cell name");
    cmd->add_option("--colour,--color", opt->colour, "net | layer")
        ->check(CLI::IsMember({"net", "layer"}))
        ->capture_default_str();
    cmd->add_option("--width", opt->width, "Output width in pixels")->capture_default_str();
    cmd->add_option("--clip", opt->clip, "Window x0 y0 x1 y1 in database units")->expected(4);
    cmd->add_flag("--no-instances", opt->no_instances, "Do not outline cell abutment boxes");
    cmd->add_option("--max-level", opt->max_level,
                    "Draw only conductor levels 0..N (0 = li1, 5 = met5)")
        ->capture_default_str();
    cmd->add_flag("--dim-power", opt->dim_power, "Draw supply nets in grey");
    cmd->add_option("--highlight", opt->highlight, "Draw only this net in colour");
    cmd->add_flag("--physical", opt->physical, "Include decap / tap / filler cells");
    cmd->callback([opt] { run_export(*opt); });
}

}  // namespace asicrev::app
