#include "asicrev/netlist/compare.hpp"
#include "asicrev/netlist/verilog_reader.hpp"
#include "asicrev/netlist/verilog_writer.hpp"
#include "asicrev/tech/std_cells.hpp"

#include "commands.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <vector>

namespace asicrev::app {

namespace {

struct CompareCliOptions {
    std::string left;
    std::string right;
    bool show_mapping = false;
    bool with_power = false;
    bool with_physical = false;
    std::string write_renamed;
};

/// Rewrite `nl` using the names its counterpart uses, following a proven
/// isomorphism. Useful in its own right - it hands the recovered design back
/// its original signal names - and it is what lets an external equivalence
/// checker pair up internal nodes instead of only the primary ports.
netlist::Netlist apply_names(const netlist::Netlist& nl, const netlist::CompareResult& r) {
    netlist::Netlist out = nl;
    for (netlist::Net& n : out.nets) {
        const auto it = r.net_map.find(n.name);
        if (it != r.net_map.end()) {
            n.name = it->second;
        }
    }
    for (netlist::Instance& i : out.instances) {
        const auto it = r.instance_map.find(i.name);
        if (it != r.instance_map.end()) {
            i.name = it->second;
        }
    }
    for (netlist::Port& p : out.ports) {
        if (p.net != netlist::kNoNet) {
            p.name = out.nets[p.net].name;
        }
    }
    return out;
}

void run_compare(const CompareCliOptions& opt) {
    const netlist::Netlist a = netlist::read_structural_verilog(opt.left);
    const netlist::Netlist b = netlist::read_structural_verilog(opt.right);

    fmt::print("left  : {} - {} instances, {} nets\n", a.module_name, a.instances.size(),
               a.nets.size());
    fmt::print("right : {} - {} instances, {} nets\n", b.module_name, b.instances.size(),
               b.nets.size());

    netlist::CompareOptions co;
    co.ignore_power_nets = !opt.with_power;
    co.ignore_physical_cells = !opt.with_physical;
    const netlist::CompareResult r =
        netlist::compare_netlists(a, b, tech::CellLibrary::sky130_hd(), co);

    fmt::print("refinement rounds: {}, backtracks: {}\n", r.refinement_rounds, r.backtracks);

    if (r.equivalent) {
        fmt::print("\nRESULT: the two netlists are structurally identical "
                   "(graph isomorphism found; {} instances and {} nets matched)\n",
                   r.instance_map.size(), r.net_map.size());
        if (!opt.write_renamed.empty()) {
            netlist::WriteOptions wo;
            wo.include_power_pins = opt.with_power;
            wo.include_physical_cells = opt.with_physical;
            wo.header_comment = fmt::format(
                "{} rewritten with the names recovered from {} via the proven isomorphism.",
                opt.left, opt.right);
            std::ofstream out(opt.write_renamed);
            netlist::write_verilog(out, apply_names(a, r), tech::CellLibrary::sky130_hd(), wo);
            fmt::print("wrote {}\n", opt.write_renamed);
        }
        if (opt.show_mapping) {
            std::vector<std::pair<std::string, std::string>> pairs(r.instance_map.begin(),
                                                                   r.instance_map.end());
            std::sort(pairs.begin(), pairs.end());
            fmt::print("\ninstance mapping (left -> right):\n");
            for (const auto& [l, rr] : pairs) {
                fmt::print("  {:<28} {}\n", l, rr);
            }
            std::vector<std::pair<std::string, std::string>> nets(r.net_map.begin(),
                                                                  r.net_map.end());
            std::sort(nets.begin(), nets.end());
            fmt::print("\nnet mapping (left -> right):\n");
            for (const auto& [l, rr] : nets) {
                fmt::print("  {:<28} {}\n", l, rr);
            }
        }
    } else {
        fmt::print("\nRESULT: the netlists differ\n");
        for (const std::string& d : r.differences) {
            fmt::print("  - {}\n", d);
        }
    }
}

}  // namespace

void register_compare(CLI::App& root) {
    auto opt = std::make_shared<CompareCliOptions>();
    CLI::App* cmd = root.add_subcommand(
        "compare", "Check two structural Verilog netlists for graph isomorphism");
    cmd->add_option("left", opt->left, "First netlist")->required()->check(CLI::ExistingFile);
    cmd->add_option("right", opt->right, "Second netlist")->required()->check(CLI::ExistingFile);
    cmd->add_flag("--mapping", opt->show_mapping, "Print the recovered name correspondence");
    cmd->add_flag("--with-power", opt->with_power, "Include power nets in the comparison");
    cmd->add_flag("--with-physical", opt->with_physical,
                  "Include decap / tap / filler instances in the comparison");
    cmd->add_option("--write-renamed", opt->write_renamed,
                    "Write the left netlist using the right netlist's names");
    cmd->callback([opt] { run_compare(*opt); });
}

}  // namespace asicrev::app
