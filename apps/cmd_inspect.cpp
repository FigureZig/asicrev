#include "asicrev/gds/flatten.hpp"
#include "asicrev/gds/reader.hpp"
#include "asicrev/tech/sky130.hpp"

#include "commands.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <map>
#include <string>

namespace asicrev::app {

namespace {

struct InspectOptions {
    std::string gds;
    bool show_layers = false;
    bool show_cells = false;
    bool show_texts = false;
};

void run_inspect(const InspectOptions& opt) {
    const gds::Library lib = gds::read_gds(opt.gds);

    fmt::print("library      : {}\n", lib.name);
    fmt::print("database unit: {:g} m ({:g} nm per dbu)\n", lib.db_unit_meters,
               lib.db_unit_meters * 1e9);
    fmt::print("cells        : {}\n", lib.cells.size());

    const std::vector<const gds::Cell*> tops = lib.top_cells();
    std::vector<std::string> top_names;
    top_names.reserve(tops.size());
    for (const gds::Cell* c : tops) {
        top_names.push_back(c->name);
    }
    fmt::print("top cell(s)  : {}\n", fmt::join(top_names, ", "));

    std::size_t boundaries = 0;
    std::size_t paths = 0;
    std::size_t refs = 0;
    std::size_t texts = 0;
    std::map<gds::LayerKey, std::size_t> layer_hist;
    std::map<std::string, std::size_t> ref_hist;
    for (const gds::Cell& c : lib.cells) {
        boundaries += c.boundaries.size();
        paths += c.paths.size();
        refs += c.references.size();
        texts += c.texts.size();
        for (const gds::Boundary& b : c.boundaries) {
            ++layer_hist[b.layer];
        }
        for (const gds::Path& p : c.paths) {
            ++layer_hist[p.layer];
        }
        for (const gds::Reference& r : c.references) {
            ref_hist[r.cell_name] += static_cast<std::size_t>(std::max<std::int16_t>(r.rows, 1)) *
                                     static_cast<std::size_t>(std::max<std::int16_t>(r.columns, 1));
        }
    }
    fmt::print("elements     : {} boundaries, {} paths, {} refs, {} texts\n", boundaries, paths,
               refs, texts);

    if (opt.show_layers) {
        const tech::Technology& t = tech::sky130();
        fmt::print("\nlayer/datatype histogram:\n");
        for (const auto& [key, count] : layer_hist) {
            std::string role = "-";
            if (const auto conductor = t.conductor_of_shape(key)) {
                role = t.conductors[*conductor].name;
            } else if (const auto cut = t.cut_of_shape(key)) {
                role = t.cuts[*cut].name;
            }
            fmt::print("  {:>4}/{:<3} {:>8}  {}\n", key.layer, key.datatype, count, role);
        }
    }

    if (opt.show_cells) {
        fmt::print("\nreferenced cells:\n");
        std::vector<std::pair<std::string, std::size_t>> sorted(ref_hist.begin(), ref_hist.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& [name, count] : sorted) {
            fmt::print("  {:>6}  {}\n", count, name);
        }
    }

    if (opt.show_texts) {
        fmt::print("\nlabels per cell:\n");
        for (const gds::Cell& c : lib.cells) {
            if (c.texts.empty()) {
                continue;
            }
            std::vector<std::string> values;
            values.reserve(c.texts.size());
            for (const gds::Text& t : c.texts) {
                values.push_back(fmt::format("{}@{}/{}", t.value, t.layer.layer, t.layer.datatype));
            }
            fmt::print("  {:<36} {}\n", c.name, fmt::join(values, " "));
        }
    }
}

}  // namespace

void register_inspect(CLI::App& root) {
    auto opt = std::make_shared<InspectOptions>();
    CLI::App* cmd = root.add_subcommand("inspect", "Summarise the contents of a GDSII file");
    cmd->add_option("gds", opt->gds, "GDSII stream file")->required()->check(CLI::ExistingFile);
    cmd->add_flag("--layers", opt->show_layers, "Print the layer/datatype histogram");
    cmd->add_flag("--cells", opt->show_cells, "Print the placed-cell histogram");
    cmd->add_flag("--texts", opt->show_texts, "Print all text labels grouped by cell");
    cmd->callback([opt] { run_inspect(*opt); });
}

}  // namespace asicrev::app
