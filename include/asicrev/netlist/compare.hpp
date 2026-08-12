#pragma once

#include "asicrev/netlist/netlist.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace asicrev::netlist {

struct CompareOptions {
    bool ignore_physical_cells = true;
    bool ignore_power_nets = true;
};

struct CompareResult {
    bool equivalent = false;
    std::vector<std::string> differences;

    /// Recovered correspondence, only filled in when `equivalent`.
    std::unordered_map<std::string, std::string> instance_map;  ///< left -> right
    std::unordered_map<std::string, std::string> net_map;       ///< left -> right

    std::size_t refinement_rounds = 0;
    std::size_t backtracks = 0;
};

/// Decide whether two gate-level netlists are the same circuit up to renaming.
///
/// Works on the instance/net bipartite graph: colours are refined
/// Weisfeiler-Lehman style using (cell type, pin name) edge labels, then a
/// backtracking search turns the stable colouring into an explicit isomorphism.
/// Nets that carry a layout name (ports) are pinned to their counterpart, which
/// makes the search terminate immediately on real netlists.
CompareResult compare_netlists(const Netlist& left, const Netlist& right,
                               const tech::CellLibrary& cells, const CompareOptions& options = {});

}  // namespace asicrev::netlist
