#pragma once

#include "asicrev/netlist/netlist.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <ostream>
#include <string>

namespace asicrev::netlist {

struct WriteOptions {
    bool include_power_pins = false;
    bool include_physical_cells = false;
    std::string header_comment;
};

/// Emit a structural Verilog netlist in the same shape as a Yosys `write_verilog`
/// result, so it can be diffed or fed straight to iverilog / yosys.
void write_verilog(std::ostream& out, const Netlist& nl, const tech::CellLibrary& cells,
                   const WriteOptions& options = {});

std::string to_verilog(const Netlist& nl, const tech::CellLibrary& cells,
                       const WriteOptions& options = {});

/// Escape a name for Verilog: identifiers with `[`, `/`, `.` etc. need the
/// backslash-escaped form followed by a space.
std::string escape_identifier(const std::string& name);

}  // namespace asicrev::netlist
