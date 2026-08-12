#pragma once

#include "asicrev/extract/extractor.hpp"
#include "asicrev/tech/sky130.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace asicrev::svg {

enum class ColourBy { Net, Layer };

struct SvgOptions {
    ColourBy colour_by = ColourBy::Net;
    int width_px = 1600;                          ///< output width; height follows the aspect ratio
    std::optional<Rect> clip;                     ///< draw only this window, in database units
    bool draw_instances = true;                   ///< outline the standard-cell abutment boxes
    double stroke = 0.0;                          ///< outline width in database units; 0 = none
    std::size_t max_level = 5;                    ///< draw conductor levels 0..max_level only
    bool dim_power = false;                       ///< draw supply nets in grey so signals stand out
    std::size_t highlight_net = netlist::kNoNet;  ///< draw this net alone in colour
    std::string background = "#ffffff";
};

/// Render the extracted rectangles. Colouring by net makes the recovered
/// connectivity directly visible: one colour per electrical node, across every
/// metal level, which is exactly the thing the extraction claims to know.
void write_svg(std::ostream& out, const extract::ExtractResult& result,
               const tech::Technology& technology, const SvgOptions& options = {});

}  // namespace asicrev::svg
