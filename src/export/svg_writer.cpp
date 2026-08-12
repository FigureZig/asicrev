#include "asicrev/export/svg_writer.hpp"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace asicrev::svg {

namespace {

/// Deterministic, well-spread colour per index. The golden-ratio hue step keeps
/// neighbouring nets visually distinct, which matters because adjacent tracks
/// are exactly the pairs a reader wants to tell apart.
std::string colour_for(std::size_t index, double saturation, double value) {
    const double hue = std::fmod(static_cast<double>(index) * 137.50776405, 360.0);
    const double c = value * saturation;
    const double x = c * (1.0 - std::fabs(std::fmod(hue / 60.0, 2.0) - 1.0));
    const double m = value - c;
    double r = 0;
    double g = 0;
    double b = 0;
    if (hue < 60) {
        r = c;
        g = x;
    } else if (hue < 120) {
        r = x;
        g = c;
    } else if (hue < 180) {
        g = c;
        b = x;
    } else if (hue < 240) {
        g = x;
        b = c;
    } else if (hue < 300) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    const auto q = [&](double v) { return static_cast<int>(std::lround((v + m) * 255.0)); };
    return fmt::format("#{:02x}{:02x}{:02x}", q(r), q(g), q(b));
}

/// Distinct, deliberately chosen colours for the six conductor levels, ordered
/// bottom-up so the eye reads height as hue.
const char* layer_colour(std::size_t conductor) {
    static const char* palette[] = {"#c0392b", "#2e86c1", "#28b463",
                                    "#f39c12", "#8e44ad", "#16a085"};
    return palette[conductor % 6];
}

}  // namespace

void write_svg(std::ostream& out, const extract::ExtractResult& result,
               const tech::Technology& technology, const SvgOptions& options) {
    if (result.geometry.empty()) {
        return;
    }

    Rect view = options.clip.value_or(Rect{});
    if (!options.clip.has_value()) {
        view = result.geometry.front().rect;
        for (const extract::ExtractedRect& r : result.geometry) {
            view.expand(r.rect);
        }
    }

    const double span_x = static_cast<double>(std::max<Dbu>(view.width(), 1));
    const double span_y = static_cast<double>(std::max<Dbu>(view.height(), 1));
    const double scale = static_cast<double>(options.width_px) / span_x;
    const int height_px = std::max(1, static_cast<int>(std::lround(span_y * scale)));

    // SVG's y axis points down; layout's points up.
    const auto sx = [&](Dbu x) { return (static_cast<double>(x - view.xlo)) * scale; };
    const auto sy = [&](Dbu y) { return (static_cast<double>(view.yhi - y)) * scale; };

    fmt::print(out,
               "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{}\" height=\"{}\" "
               "viewBox=\"0 0 {} {}\">\n",
               options.width_px, height_px, options.width_px, height_px);
    fmt::print(out, "<rect width=\"100%\" height=\"100%\" fill=\"{}\"/>\n", options.background);

    if (options.draw_instances) {
        out << "<g fill=\"none\" stroke=\"#b9c2cc\" stroke-width=\"0.7\">\n";
        for (const Rect& box : result.instance_boxes) {
            if (options.clip.has_value() && !box.touches(view)) {
                continue;
            }
            fmt::print(out,
                       "<rect x=\"{:.2f}\" y=\"{:.2f}\" width=\"{:.2f}\" height=\"{:.2f}\"/>\n",
                       sx(box.xlo), sy(box.yhi), static_cast<double>(box.width()) * scale,
                       static_cast<double>(box.height()) * scale);
        }
        out << "</g>\n";
    }

    // Draw the stack bottom-up so upper metal visibly crosses over lower metal.
    const std::size_t top_level = std::min(options.max_level, technology.conductors.size() - 1);
    for (std::size_t level = 0; level <= top_level; ++level) {
        bool opened = false;
        for (const extract::ExtractedRect& r : result.geometry) {
            if (r.conductor != level) {
                continue;
            }
            if (options.clip.has_value() && !r.rect.touches(view)) {
                continue;
            }
            if (!opened) {
                fmt::print(out, "<g id=\"{}\">\n", technology.conductors[level].name);
                opened = true;
            }
            const bool supply = r.net != netlist::kNoNet &&
                                result.netlist.nets[r.net].kind != netlist::NetKind::Signal;

            std::string fill;
            if (options.highlight_net != netlist::kNoNet) {
                // One net in colour, the rest as context. This is the picture
                // that shows connectivity was actually recovered: a single
                // electrical node traced across every level it uses.
                fill = r.net == options.highlight_net ? "#d62828" : "#e8ebee";
            } else if (options.dim_power && supply) {
                fill = "#e4e7ea";
            } else if (options.colour_by == ColourBy::Layer) {
                fill = layer_colour(level);
            } else if (r.net == netlist::kNoNet) {
                fill = "#d5d8dc";  // geometry that belongs to no recovered net
            } else {
                // Vary value slightly with the level so a net's vertical extent
                // stays legible while keeping one hue per net.
                const double value = 0.95 - 0.06 * static_cast<double>(level);
                fill = colour_for(r.net, 0.68, value);
            }
            fmt::print(out,
                       "<rect x=\"{:.2f}\" y=\"{:.2f}\" width=\"{:.2f}\" height=\"{:.2f}\" "
                       "fill=\"{}\"",
                       sx(r.rect.xlo), sy(r.rect.yhi), static_cast<double>(r.rect.width()) * scale,
                       static_cast<double>(r.rect.height()) * scale, fill);
            if (options.stroke > 0.0) {
                fmt::print(out, " stroke=\"#333\" stroke-width=\"{:.2f}\"", options.stroke * scale);
            }
            out << "/>\n";
        }
        if (opened) {
            out << "</g>\n";
        }
    }

    out << "</svg>\n";
}

}  // namespace asicrev::svg
