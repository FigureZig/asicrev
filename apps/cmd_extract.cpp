#include "asicrev/def/def_reader.hpp"
#include "asicrev/extract/extractor.hpp"
#include "asicrev/gds/reader.hpp"
#include "asicrev/netlist/json_writer.hpp"
#include "asicrev/netlist/verilog_writer.hpp"
#include "asicrev/tech/sky130.hpp"
#include "asicrev/tech/std_cells.hpp"

#include "commands.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace asicrev::app {

namespace {

struct ExtractCliOptions {
    std::string gds;
    std::string top;
    std::string out_verilog;
    std::string out_json;
    std::string out_stubs;
    std::string check_def;
    bool keep_power = false;
    bool keep_physical = false;
    bool quiet = false;
};

/// Cross-check the extracted placement and connectivity against the DEF that
/// produced the layout. DEF is never an input to extraction: this is purely a
/// scoring step for the warm-up, where the ground truth happens to be public.
void check_against_def(const netlist::Netlist& nl, const def::Design& design) {
    fmt::print("\n=== DEF cross-check ({}) ===\n", design.name);

    // Placement: every DEF component should sit where we found an instance.
    std::multiset<std::tuple<std::string, Dbu, Dbu>> from_def;
    for (const def::Component& c : design.components) {
        from_def.emplace(c.cell, c.position.x, c.position.y);
    }
    // DEF places the abutment box, so compare its lower-left corner rather than
    // the GDS cell origin, which sits at the top edge for flipped rows.
    std::multiset<std::tuple<std::string, Dbu, Dbu>> from_gds;
    for (const netlist::Instance& i : nl.instances) {
        from_gds.emplace(i.cell, i.abutment.xlo, i.abutment.ylo);
    }

    std::size_t matched = 0;
    std::vector<std::string> missing;
    for (const auto& key : from_def) {
        if (from_gds.contains(key)) {
            ++matched;
        } else if (missing.size() < 10) {
            missing.push_back(
                fmt::format("{} @ ({}, {})", std::get<0>(key), std::get<1>(key), std::get<2>(key)));
        }
    }
    fmt::print("placement : {}/{} DEF components found at the same origin in the GDS\n", matched,
               from_def.size());
    if (!missing.empty()) {
        fmt::print("            missing e.g. {}\n", fmt::join(missing, ", "));
    }

    // Connectivity: compare the pin sets of each DEF signal net with ours.
    std::map<std::string, std::set<std::string>> def_nets;
    for (const def::Net& n : design.nets) {
        if (n.special) {
            continue;
        }
        for (const def::NetPin& p : n.pins) {
            def_nets[n.name].insert(p.instance.empty() ? fmt::format("PIN:{}", p.pin)
                                                       : fmt::format("{}.{}", p.instance, p.pin));
        }
    }

    // Our instance names are synthetic, so compare the *shape* of the
    // partition: the multiset of (cell, pin) tuples on each net.
    auto signature = [](const std::set<std::string>& pins,
                        const std::map<std::string, std::string>& inst_cell) {
        std::multiset<std::string> sig;
        for (const std::string& p : pins) {
            const auto dot = p.rfind('.');
            if (dot == std::string::npos) {
                sig.insert(p);  // top-level PIN
                continue;
            }
            const std::string inst = p.substr(0, dot);
            const std::string pin = p.substr(dot + 1);
            const auto it = inst_cell.find(inst);
            sig.insert(fmt::format("{}.{}", it == inst_cell.end() ? "?" : it->second, pin));
        }
        std::vector<std::string> flat(sig.begin(), sig.end());
        return fmt::format("{}", fmt::join(flat, "|"));
    };

    std::map<std::string, std::string> def_inst_cell;
    for (const def::Component& c : design.components) {
        def_inst_cell[c.name] = c.cell;
    }
    std::multiset<std::string> def_sigs;
    for (const auto& [name, pins] : def_nets) {
        def_sigs.insert(signature(pins, def_inst_cell));
    }

    std::map<std::string, std::string> our_inst_cell;
    std::map<std::size_t, std::set<std::string>> our_nets;
    for (const netlist::Instance& i : nl.instances) {
        our_inst_cell[i.name] = i.cell;
        for (const netlist::InstancePin& p : i.pins) {
            if (p.net != netlist::kNoNet) {
                our_nets[p.net].insert(fmt::format("{}.{}", i.name, p.pin));
            }
        }
    }
    for (const netlist::Port& p : nl.ports) {
        if (p.net != netlist::kNoNet) {
            our_nets[p.net].insert(fmt::format("PIN:{}", p.name));
        }
    }
    std::multiset<std::string> our_sigs;
    for (const auto& [net, pins] : our_nets) {
        if (nl.nets[net].kind != netlist::NetKind::Signal) {
            continue;
        }
        our_sigs.insert(signature(pins, our_inst_cell));
    }

    std::size_t net_matches = 0;
    std::multiset<std::string> remaining = our_sigs;
    for (const std::string& s : def_sigs) {
        const auto it = remaining.find(s);
        if (it != remaining.end()) {
            ++net_matches;
            remaining.erase(it);
        }
    }
    fmt::print("nets      : {}/{} DEF signal nets have an identical pin set in the extraction\n",
               net_matches, def_sigs.size());
    if (net_matches != def_sigs.size()) {
        std::size_t shown = 0;
        for (const auto& [name, pins] : def_nets) {
            const std::string sig = signature(pins, def_inst_cell);
            if (!our_sigs.contains(sig) && shown++ < 5) {
                fmt::print("            unmatched DEF net '{}': {}\n", name, sig);
            }
        }
    }
}

void run_extract(const ExtractCliOptions& opt) {
    const gds::Library lib = gds::read_gds(opt.gds);
    const tech::CellLibrary& cells = tech::CellLibrary::sky130_hd();

    extract::ExtractOptions eo;
    eo.top_cell = opt.top;
    eo.keep_physical_cells = opt.keep_physical;
    eo.keep_power_pins = opt.keep_power;

    const extract::ExtractResult result = extract_netlist(lib, tech::sky130(), cells, eo);
    const extract::ExtractStats& s = result.stats;

    if (!opt.quiet) {
        fmt::print("=== extraction ({}) ===\n", result.netlist.module_name);
        fmt::print("placed cells    : {}  (logic: {})\n", s.instances, s.logic_instances);
        fmt::print("polygons        : {} -> {} rectangles\n", s.polygons, s.rectangles);
        fmt::print("labels          : {}\n", s.labels);
        fmt::print("nets            : {}  (signal: {})\n", s.nets_total, s.nets_signal);
        fmt::print("ports           : {}\n", result.netlist.ports.size());
        for (const netlist::Port& p : result.netlist.ports) {
            const char* dir = p.direction == netlist::PortDirection::Input    ? "input"
                              : p.direction == netlist::PortDirection::Output ? "output"
                                                                              : "inout";
            fmt::print("                  {:<6} {}\n", dir, p.name);
        }

        const auto hist = result.netlist.cell_histogram();
        std::vector<std::pair<std::string, std::size_t>> sorted(hist.begin(), hist.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        fmt::print("cell usage      :\n");
        for (const auto& [cell, count] : sorted) {
            fmt::print("                  {:>4}  {}\n", count, cell);
        }

        if (s.inferred_body_pins != 0) {
            fmt::print("body pins       : {} VPB/VNB connections inferred from the rails\n",
                       s.inferred_body_pins);
        }
        if (!s.unknown_cells.empty()) {
            fmt::print("unknown cells   : {}\n", fmt::join(s.unknown_cells, ", "));
        }
        if (s.unbound_pins != 0 || s.split_pins != 0) {
            fmt::print("PROBLEMS        : {} unbound labels, {} split pins\n", s.unbound_pins,
                       s.split_pins);
        }
        for (const std::string& w : s.warnings) {
            fmt::print("warning         : {}\n", w);
        }
    }

    if (!opt.out_verilog.empty()) {
        netlist::WriteOptions wo;
        wo.include_power_pins = opt.keep_power;
        wo.include_physical_cells = opt.keep_physical;
        wo.header_comment =
            fmt::format("Extracted from {} by asicrev.\nDo not edit: regenerate instead.",
                        std::filesystem::path(opt.gds).filename().string());
        std::ofstream out(opt.out_verilog);
        netlist::write_verilog(out, result.netlist, cells, wo);
        fmt::print("wrote {}\n", opt.out_verilog);
    }

    if (!opt.out_json.empty()) {
        nlohmann::ordered_json j;
        j["stats"] = netlist::to_json(result.stats);
        j["netlist"] = netlist::to_json(result.netlist);
        std::ofstream out(opt.out_json);
        out << j.dump(2) << "\n";
        fmt::print("wrote {}\n", opt.out_json);
    }

    if (!opt.out_stubs.empty()) {
        std::vector<std::string> used;
        for (const netlist::Instance& i : result.netlist.instances) {
            used.push_back(i.cell);
        }
        std::ofstream out(opt.out_stubs);
        out << cells.verilog_stubs(used);
        fmt::print("wrote {}\n", opt.out_stubs);
    }

    if (!opt.check_def.empty()) {
        check_against_def(result.netlist, def::read_def(opt.check_def));
    }
}

}  // namespace

void register_extract(CLI::App& root) {
    auto opt = std::make_shared<ExtractCliOptions>();
    CLI::App* cmd =
        root.add_subcommand("extract", "Recover a gate-level netlist from a GDSII layout");
    cmd->add_option("gds", opt->gds, "GDSII stream file")->required()->check(CLI::ExistingFile);
    cmd->add_option("--top", opt->top, "Top cell name (default: the unique unreferenced cell)");
    cmd->add_option("-o,--verilog", opt->out_verilog, "Write the netlist as structural Verilog");
    cmd->add_option("--json", opt->out_json, "Write the netlist and stats as JSON");
    cmd->add_option("--stubs", opt->out_stubs,
                    "Write behavioural Verilog models for the cells used");
    cmd->add_option("--check-def", opt->check_def, "Score the result against a reference DEF")
        ->check(CLI::ExistingFile);
    cmd->add_flag("--power", opt->keep_power, "Keep VPWR/VGND/VPB/VNB connections");
    cmd->add_flag("--physical", opt->keep_physical, "Keep decap / tap / filler instances");
    cmd->add_flag("-q,--quiet", opt->quiet, "Only report written files");
    cmd->callback([opt] { run_extract(*opt); });
}

}  // namespace asicrev::app
