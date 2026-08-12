#include "asicrev/netlist/verilog_reader.hpp"
#include "asicrev/sim/simulator.hpp"
#include "asicrev/sim/vcd_writer.hpp"
#include "asicrev/tech/std_cells.hpp"

#include "commands.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace asicrev::app {

namespace {

struct SimCliOptions {
    std::string netlist;
    std::string clock = "clk";
    std::string reset;
    bool reset_active_high = false;
    std::size_t reset_cycles = 2;
    std::vector<std::string> drives;     ///< NET=0|1
    std::vector<std::string> sequences;  ///< NET=010101
    std::vector<std::string> watches;
    std::string vcd;
    std::size_t cycles = 0;

    // solve mode
    std::vector<std::string> serial;
    std::size_t bits = 8;
    std::string target;
    std::size_t max_solutions = 20;
};

std::pair<std::string, std::string> split_assignment(const std::string& s) {
    const auto eq = s.find('=');
    if (eq == std::string::npos) {
        throw std::runtime_error(fmt::format("expected NET=VALUE, got '{}'", s));
    }
    return {s.substr(0, eq), s.substr(eq + 1)};
}

sim::Logic bit_to_logic(char c) {
    switch (c) {
        case '0': return sim::Logic::Zero;
        case '1': return sim::Logic::One;
        default: return sim::Logic::Unknown;
    }
}

void require_net(const netlist::Netlist& nl, const std::string& name, const char* what) {
    if (nl.net_by_name(name) == netlist::kNoNet) {
        throw std::runtime_error(
            fmt::format("{} net '{}' does not exist in the netlist", what, name));
    }
}

/// Drive `serial` inputs with `bits` bits each (MSB first) and report the
/// target net after the last shift.
struct ShiftHarness {
    sim::Simulator& simulator;
    const SimCliOptions& opt;
    std::vector<std::string> serial_nets;

    void reset() {
        simulator.reset_state();
        for (const std::string& d : opt.drives) {
            const auto [net, value] = split_assignment(d);
            simulator.set_net(net, bit_to_logic(value.empty() ? 'x' : value[0]));
        }
        if (!opt.reset.empty()) {
            simulator.set_net(opt.reset,
                              opt.reset_active_high ? sim::Logic::One : sim::Logic::Zero);
            simulator.settle();
            for (std::size_t i = 0; i < opt.reset_cycles; ++i) {
                simulator.clock_edge(opt.clock);
            }
            simulator.set_net(opt.reset,
                              opt.reset_active_high ? sim::Logic::Zero : sim::Logic::One);
            simulator.settle();
        } else {
            simulator.settle();
        }
    }

    /// `words[i]` supplies the bit stream for `serial_nets[i]`, MSB first.
    void shift(const std::vector<std::uint64_t>& words) {
        for (std::size_t b = 0; b < opt.bits; ++b) {
            for (std::size_t s = 0; s < serial_nets.size(); ++s) {
                const std::uint64_t bit = (words[s] >> (opt.bits - 1 - b)) & 1U;
                simulator.set_net(serial_nets[s], bit != 0 ? sim::Logic::One : sim::Logic::Zero);
            }
            simulator.settle();
            simulator.clock_edge(opt.clock);
        }
    }
};

void run_solve(const SimCliOptions& opt, const netlist::Netlist& nl, sim::Simulator& simulator) {
    require_net(nl, opt.target, "target");
    for (const std::string& s : opt.serial) {
        require_net(nl, s, "serial input");
    }
    if (opt.bits * opt.serial.size() > 26) {
        throw std::runtime_error(
            fmt::format("search space of 2^{} is too large for exhaustive search",
                        opt.bits * opt.serial.size()));
    }

    ShiftHarness harness{simulator, opt, opt.serial};
    const std::uint64_t span = 1ULL << opt.bits;
    const std::size_t n = opt.serial.size();

    fmt::print("searching {} combinations of {} x {} bits for {} == 1 ...\n",
               static_cast<std::uint64_t>(1) << (opt.bits * n), n, opt.bits, opt.target);

    std::vector<std::uint64_t> words(n, 0);
    std::vector<std::vector<std::uint64_t>> solutions;
    const std::uint64_t total = static_cast<std::uint64_t>(1) << (opt.bits * n);

    for (std::uint64_t combo = 0; combo < total; ++combo) {
        std::uint64_t rest = combo;
        for (std::size_t i = 0; i < n; ++i) {
            words[i] = rest % span;
            rest /= span;
        }
        harness.reset();
        harness.shift(words);
        if (simulator.get_net(opt.target) == sim::Logic::One) {
            solutions.push_back(words);
        }
    }

    fmt::print("found {} input combination(s) that drive {} high\n", solutions.size(), opt.target);
    std::uint64_t sum_check = 0;
    bool sum_constant = true;
    for (std::size_t i = 0; i < solutions.size(); ++i) {
        std::uint64_t sum = 0;
        for (std::uint64_t w : solutions[i]) {
            sum += w;
        }
        if (i == 0) {
            sum_check = sum;
        } else if (sum != sum_check) {
            sum_constant = false;
        }
        if (i < opt.max_solutions) {
            std::vector<std::string> parts;
            for (std::size_t k = 0; k < n; ++k) {
                parts.push_back(fmt::format("{}={:3} (0b{:08b})", opt.serial[k], solutions[i][k],
                                            solutions[i][k]));
            }
            fmt::print("  {}  sum={}\n", fmt::join(parts, "  "), sum);
        }
    }
    if (solutions.size() > opt.max_solutions) {
        fmt::print("  ... {} more\n", solutions.size() - opt.max_solutions);
    }
    if (!solutions.empty() && sum_constant) {
        fmt::print("\nevery solution satisfies {} == {}\n", fmt::join(opt.serial, " + "),
                   sum_check);
    }
}

void run_trace(const SimCliOptions& opt, const netlist::Netlist& nl, sim::Simulator& simulator) {
    std::vector<std::pair<std::string, std::string>> seqs;
    std::size_t cycles = opt.cycles;
    for (const std::string& s : opt.sequences) {
        auto [net, bits] = split_assignment(s);
        require_net(nl, net, "sequence");
        cycles = std::max(cycles, bits.size());
        seqs.emplace_back(std::move(net), std::move(bits));
    }
    if (cycles == 0) {
        cycles = 16;
    }

    std::vector<std::string> watched = opt.watches;
    if (watched.empty()) {
        for (const netlist::Port& p : nl.ports) {
            watched.push_back(p.name);
        }
    }

    std::ofstream vcd_file;
    std::unique_ptr<sim::VcdWriter> vcd;
    if (!opt.vcd.empty()) {
        vcd_file.open(opt.vcd);
        vcd = std::make_unique<sim::VcdWriter>(vcd_file, nl.module_name);
        vcd->add_signal(opt.clock);
        if (!opt.reset.empty()) {
            vcd->add_signal(opt.reset);
        }
        for (const auto& [net, bits] : seqs) {
            vcd->add_signal(net);
        }
        for (const std::string& w : watched) {
            vcd->add_signal(w);
        }
        for (const std::string& d : opt.drives) {
            vcd->add_signal(split_assignment(d).first);
        }
        vcd->finish_header();
    }

    std::uint64_t time = 0;
    const std::uint64_t half = 5;  // 5 ns half period at the 1 ps timescale below

    auto snapshot = [&](bool clock_high) {
        if (vcd == nullptr) {
            return;
        }
        vcd->advance_to(time);
        vcd->set(opt.clock, clock_high ? sim::Logic::One : sim::Logic::Zero);
        for (const std::string& w : watched) {
            vcd->set(w, simulator.get_net(w));
        }
        if (!opt.reset.empty()) {
            vcd->set(opt.reset, simulator.get_net(opt.reset));
        }
        for (const auto& [net, bits] : seqs) {
            vcd->set(net, simulator.get_net(net));
        }
        for (const std::string& d : opt.drives) {
            const std::string net = split_assignment(d).first;
            vcd->set(net, simulator.get_net(net));
        }
        vcd->flush_time();
        time += half * 1000;
    };

    simulator.reset_state();
    for (const std::string& d : opt.drives) {
        const auto [net, value] = split_assignment(d);
        require_net(nl, net, "drive");
        simulator.set_net(net, bit_to_logic(value.empty() ? 'x' : value[0]));
    }

    if (!opt.reset.empty()) {
        require_net(nl, opt.reset, "reset");
        simulator.set_net(opt.reset, opt.reset_active_high ? sim::Logic::One : sim::Logic::Zero);
        simulator.settle();
        for (std::size_t i = 0; i < opt.reset_cycles; ++i) {
            snapshot(false);
            simulator.clock_edge(opt.clock);
            snapshot(true);
        }
        simulator.set_net(opt.reset, opt.reset_active_high ? sim::Logic::Zero : sim::Logic::One);
        simulator.settle();
    }

    fmt::print("{:>5}  {}\n", "cycle", fmt::join(watched, "  "));
    for (std::size_t c = 0; c < cycles; ++c) {
        for (const auto& [net, bits] : seqs) {
            const char bit = c < bits.size() ? bits[c] : bits.empty() ? 'x' : bits.back();
            simulator.set_net(net, bit_to_logic(bit));
        }
        simulator.settle();
        snapshot(false);
        simulator.clock_edge(opt.clock);
        snapshot(true);

        std::vector<std::string> values;
        values.reserve(watched.size());
        for (const std::string& w : watched) {
            values.push_back(std::string(1, sim::to_char(simulator.get_net(w))));
        }
        fmt::print("{:>5}  {}\n", c, fmt::join(values, "  "));
    }

    if (vcd != nullptr) {
        vcd->flush_time();
        fmt::print("wrote {}\n", opt.vcd);
    }
}

void run_sim(const SimCliOptions& opt) {
    const netlist::Netlist nl = netlist::read_structural_verilog(opt.netlist);
    sim::Simulator simulator(nl, tech::CellLibrary::sky130_hd());

    for (const std::string& w : simulator.warnings()) {
        fmt::print("warning: {}\n", w);
    }
    fmt::print("netlist : {} - {} instances, {} nets, {} flip-flops\n", nl.module_name,
               nl.instances.size(), nl.nets.size(), simulator.flip_flop_count());

    std::vector<std::string> inputs;
    for (std::size_t n : simulator.free_inputs()) {
        inputs.push_back(nl.nets[n].name);
    }
    fmt::print("free inputs: {}\n\n", fmt::join(inputs, ", "));

    require_net(nl, opt.clock, "clock");
    if (!opt.target.empty()) {
        run_solve(opt, nl, simulator);
    } else {
        run_trace(opt, nl, simulator);
    }
}

}  // namespace

void register_sim(CLI::App& root) {
    auto opt = std::make_shared<SimCliOptions>();
    CLI::App* cmd =
        root.add_subcommand("sim", "Simulate a gate-level netlist, or search its inputs");
    cmd->add_option("netlist", opt->netlist, "Structural Verilog netlist")
        ->required()
        ->check(CLI::ExistingFile);
    cmd->add_option("--clock", opt->clock, "Clock net name")->capture_default_str();
    cmd->add_option("--reset", opt->reset, "Asynchronous reset net");
    cmd->add_flag("--reset-active-high", opt->reset_active_high, "Reset asserts high");
    cmd->add_option("--reset-cycles", opt->reset_cycles, "Clock edges to hold reset")
        ->capture_default_str();
    cmd->add_option("--drive", opt->drives, "Hold a net at a constant: NET=0 or NET=1");
    cmd->add_option("--seq", opt->sequences, "Per-cycle stimulus: NET=01011010");
    cmd->add_option("--watch", opt->watches, "Nets to print (default: all ports)");
    cmd->add_option("--vcd", opt->vcd, "Write a VCD trace");
    cmd->add_option("--cycles", opt->cycles, "Number of clock cycles to run");

    cmd->add_option("--serial", opt->serial,
                    "Solve mode: shift-register serial input (repeatable)");
    cmd->add_option("--bits", opt->bits, "Solve mode: bits shifted into each serial input")
        ->capture_default_str();
    cmd->add_option("--target", opt->target, "Solve mode: net that must end up high");
    cmd->add_option("--max-solutions", opt->max_solutions, "Solve mode: how many to print")
        ->capture_default_str();

    cmd->callback([opt] { run_sim(*opt); });
}

}  // namespace asicrev::app
