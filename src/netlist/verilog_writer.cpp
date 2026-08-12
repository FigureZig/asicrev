#include "asicrev/netlist/verilog_writer.hpp"

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace asicrev::netlist {

namespace {

bool needs_escape(const std::string& name) {
    if (name.empty()) {
        return true;
    }
    if (std::isdigit(static_cast<unsigned char>(name.front())) != 0) {
        return true;
    }
    return std::any_of(name.begin(), name.end(), [](char c) {
        return (std::isalnum(static_cast<unsigned char>(c)) == 0) && c != '_' && c != '$';
    });
}

}  // namespace

std::string escape_identifier(const std::string& name) {
    if (!needs_escape(name)) {
        return name;
    }
    // Verilog escaped identifiers run until whitespace, hence the trailing space.
    return "\\" + name + " ";
}

void write_verilog(std::ostream& out, const Netlist& nl, const tech::CellLibrary& cells,
                   const WriteOptions& options) {
    if (!options.header_comment.empty()) {
        std::istringstream lines(options.header_comment);
        std::string line;
        while (std::getline(lines, line)) {
            fmt::print(out, "// {}\n", line);
        }
        out << "\n";
    }

    std::vector<const Port*> ports;
    for (const Port& p : nl.ports) {
        const bool supply = nl.nets[p.net].kind != NetKind::Signal;
        if (supply && !options.include_power_pins) {
            continue;
        }
        ports.push_back(&p);
    }

    std::vector<std::string> port_names;
    port_names.reserve(ports.size());
    for (const Port* p : ports) {
        port_names.push_back(escape_identifier(p->name));
    }
    fmt::print(out, "module {} ({});\n", escape_identifier(nl.module_name),
               fmt::join(port_names, ", "));

    for (const Port* p : ports) {
        const char* dir = "input";
        switch (p->direction) {
            case PortDirection::Output: dir = "output"; break;
            case PortDirection::InOut: dir = "inout"; break;
            case PortDirection::Input: dir = "input"; break;
        }
        fmt::print(out, " {} {};\n", dir, escape_identifier(p->name));
    }
    out << "\n";

    std::vector<std::size_t> port_nets;
    for (const Port* p : ports) {
        port_nets.push_back(p->net);
    }
    for (std::size_t i = 0; i < nl.nets.size(); ++i) {
        const Net& net = nl.nets[i];
        if (std::find(port_nets.begin(), port_nets.end(), i) != port_nets.end()) {
            continue;
        }
        if (net.kind != NetKind::Signal && !options.include_power_pins) {
            continue;
        }
        fmt::print(out, " wire {};\n", escape_identifier(net.name));
    }
    out << "\n";

    for (const Instance& inst : nl.instances) {
        const tech::CellModel* model = cells.find(inst.cell);
        if (model != nullptr && model->is_physical() && !options.include_physical_cells) {
            continue;
        }

        std::vector<std::string> connections;
        for (const InstancePin& p : inst.pins) {
            if (p.net == kNoNet) {
                continue;
            }
            const bool supply = nl.nets[p.net].kind != NetKind::Signal;
            if (supply && !options.include_power_pins) {
                continue;
            }
            connections.push_back(
                fmt::format(".{}({})", p.pin, escape_identifier(nl.nets[p.net].name)));
        }

        fmt::print(out, " {} {} (", inst.cell, escape_identifier(inst.name));
        if (connections.empty()) {
            out << ");\n";
            continue;
        }
        out << "\n";
        for (std::size_t i = 0; i < connections.size(); ++i) {
            fmt::print(out, "    {}{}\n", connections[i], i + 1 == connections.size() ? ");" : ",");
        }
    }

    out << "\nendmodule\n";
}

std::string to_verilog(const Netlist& nl, const tech::CellLibrary& cells,
                       const WriteOptions& options) {
    std::ostringstream os;
    write_verilog(os, nl, cells, options);
    return os.str();
}

}  // namespace asicrev::netlist
