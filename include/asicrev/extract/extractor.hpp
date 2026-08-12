#pragma once

#include "asicrev/gds/library.hpp"
#include "asicrev/netlist/netlist.hpp"
#include "asicrev/tech/sky130.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <string>
#include <vector>

namespace asicrev::extract {

struct ExtractOptions {
    std::string top_cell;              ///< empty -> auto-detect the unique top
    bool keep_physical_cells = false;  ///< keep decap/tap instances in the netlist
    bool keep_power_pins = false;      ///< keep VPWR/VGND/VPB/VNB connections
    /// Body pins (VPB/VNB) label the n-well and the substrate, not a routing
    /// layer, so geometric extraction cannot reach them without transistor-level
    /// device recognition. In sky130 they are unconditionally tied to the cell's
    /// own rails, so with this on they are inferred from VPWR/VGND instead of
    /// being dropped. Only meaningful together with keep_power_pins.
    bool infer_body_pins = true;
    bool keep_geometry = false;    ///< also return the decomposed rectangles
    std::string net_prefix = "n";  ///< prefix for auto-named nets
};

struct ExtractStats {
    std::size_t instances = 0;
    std::size_t logic_instances = 0;
    std::size_t polygons = 0;
    std::size_t rectangles = 0;
    std::size_t nets_total = 0;
    std::size_t nets_signal = 0;
    std::size_t labels = 0;
    std::size_t unbound_pins = 0;        ///< pin label sat on no conductor shape
    std::size_t split_pins = 0;          ///< same pin label resolved to >1 net
    std::size_t inferred_body_pins = 0;  ///< VPB/VNB tied to the rails by assumption
    std::vector<std::string> unknown_cells;
    std::vector<std::string> warnings;
};

/// One decomposed rectangle, tagged with the net the extraction assigned it.
/// Only produced when ExtractOptions::keep_geometry is set; this is what the
/// SVG exporter and any external viewer draw.
struct ExtractedRect {
    Rect rect;
    std::size_t conductor = 0;          ///< index into Technology::conductors
    std::size_t net = netlist::kNoNet;  ///< index into Netlist::nets, or kNoNet
};

struct ExtractResult {
    netlist::Netlist netlist;
    ExtractStats stats;
    std::vector<ExtractedRect> geometry;
    std::vector<Rect> instance_boxes;  ///< abutment box per kept instance
};

/// Rebuild a gate-level netlist from a finished layout by geometric extraction.
ExtractResult extract_netlist(const gds::Library& lib, const tech::Technology& technology,
                              const tech::CellLibrary& cells, const ExtractOptions& options);

}  // namespace asicrev::extract
