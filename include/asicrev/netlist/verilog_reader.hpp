#pragma once

#include "asicrev/netlist/netlist.hpp"

#include <filesystem>
#include <string>

namespace asicrev::netlist {

/// Minimal reader for *structural* gate-level Verilog: a single module made of
/// wire declarations and cell instantiations with named port connections. This
/// is exactly what synthesis emits, and all we need in order to compare our
/// extraction against a reference netlist. It is not a general Verilog parser.
Netlist read_structural_verilog(const std::filesystem::path& path);

Netlist parse_structural_verilog(const std::string& text, const std::string& origin = "<memory>");

}  // namespace asicrev::netlist
