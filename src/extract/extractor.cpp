#include "asicrev/extract/extractor.hpp"

#include "asicrev/extract/connectivity.hpp"
#include "asicrev/extract/rect_decompose.hpp"
#include "asicrev/gds/flatten.hpp"
#include "asicrev/gds/reader.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace asicrev::extract {

namespace {

/// Grid bucket size for the spatial index. sky130 routing pitches are a few
/// hundred nm, so a 2 um bucket keeps a handful of shapes per cell.
constexpr Dbu kGridSize = 2000;

const gds::Cell& pick_top(const gds::Library& lib, const std::string& requested) {
    if (!requested.empty()) {
        const gds::Cell* c = lib.find(requested);
        if (c == nullptr) {
            throw gds::ParseError(fmt::format("cell '{}' not found in library", requested));
        }
        return *c;
    }
    const std::vector<const gds::Cell*> tops = lib.top_cells();
    if (tops.empty()) {
        throw gds::ParseError("library has no top cell (every cell is referenced)");
    }
    if (tops.size() > 1) {
        std::vector<std::string> names;
        names.reserve(tops.size());
        for (const gds::Cell* c : tops) {
            names.push_back(c->name);
        }
        throw gds::ParseError(fmt::format("library has {} top cells ({}); pass --top", tops.size(),
                                          fmt::join(names, ", ")));
    }
    return *tops.front();
}

/// A referenced cell counts as a standard cell when it is a hierarchy leaf and
/// carries pin labels on a routing layer. Via and fill-geometry cells have no
/// labels, so they get inlined into the top-level routing instead.
std::unordered_set<std::string> find_std_cells(const gds::Library& lib,
                                               const tech::Technology& technology,
                                               const std::string& top_name) {
    std::unordered_set<std::string> result;
    for (const gds::Cell& c : lib.cells) {
        if (c.name == top_name || !c.references.empty()) {
            continue;
        }
        const bool has_pin_label = std::any_of(c.texts.begin(), c.texts.end(), [&](const auto& t) {
            return technology.conductor_of_label(t.layer).has_value();
        });
        if (has_pin_label) {
            result.insert(c.name);
        }
    }
    return result;
}

bool is_supply_name(const std::string& n) {
    return n == "VPWR" || n == "VGND" || n == "VPB" || n == "VNB" || n == "VDD" || n == "VSS";
}

netlist::NetKind supply_kind(const std::string& n) {
    if (n == "VPWR" || n == "VPB" || n == "VDD") {
        return netlist::NetKind::Power;
    }
    if (n == "VGND" || n == "VNB" || n == "VSS") {
        return netlist::NetKind::Ground;
    }
    return netlist::NetKind::Signal;
}

std::string instance_base_name(const std::string& cell) {
    const std::string base = tech::base_cell_name(cell);
    return base.empty() ? cell : base;
}

/// sky130 marks a standard cell's abutment box with `areaid.standardc`. Placers
/// position that box, not the cell's geometric extent, so it is what a DEF
/// `PLACED` coordinate refers to.
constexpr gds::LayerKey kAbutmentLayer{81, 4};

std::unordered_map<std::string, Rect> abutment_boxes(const gds::Library& lib) {
    std::unordered_map<std::string, Rect> boxes;
    for (const gds::Cell& c : lib.cells) {
        Rect box{};
        bool have = false;
        for (const gds::Boundary& b : c.boundaries) {
            if (b.layer != kAbutmentLayer) {
                continue;
            }
            const Rect r = bounding_box(b.points);
            if (!have) {
                box = r;
                have = true;
            } else {
                box.expand(r);
            }
        }
        if (have) {
            boxes.emplace(c.name, box);
        }
    }
    return boxes;
}

Rect transformed_box(const Rect& box, const Transform& xf) {
    const std::vector<Point> corners = {
        xf.apply(Point{box.xlo, box.ylo}), xf.apply(Point{box.xhi, box.ylo}),
        xf.apply(Point{box.xhi, box.yhi}), xf.apply(Point{box.xlo, box.yhi})};
    return bounding_box(corners);
}

}  // namespace

ExtractResult extract_netlist(const gds::Library& lib, const tech::Technology& technology,
                              const tech::CellLibrary& cells, const ExtractOptions& options) {
    ExtractResult result;
    ExtractStats& stats = result.stats;

    const gds::Cell& top = pick_top(lib, options.top_cell);
    const std::unordered_set<std::string> std_cells = find_std_cells(lib, technology, top.name);

    const gds::FlatLayout flat =
        gds::flatten(lib, top, [&](const std::string& name) { return std_cells.contains(name); });
    const std::unordered_map<std::string, Rect> abutments = abutment_boxes(lib);

    stats.instances = flat.instances.size();
    stats.polygons = flat.polygons.size();

    // ---------------------------------------------------------------- geometry

    ConnectivityBuilder conn(technology.conductors.size(), kGridSize);
    std::size_t diagonal_shapes = 0;

    for (std::size_t i = 0; i < flat.polygons.size(); ++i) {
        const gds::FlatPolygon& poly = flat.polygons[i];
        const auto conductor = technology.conductor_of_shape(poly.layer);
        const auto cut = technology.cut_of_shape(poly.layer);
        if (!conductor.has_value() && !cut.has_value()) {
            continue;
        }
        bool diagonal = false;
        const std::vector<Rect> pieces = decompose_polygon(poly.points, &diagonal);
        diagonal_shapes += diagonal ? 1 : 0;
        for (const Rect& r : pieces) {
            if (conductor.has_value()) {
                conn.add_conductor(r, *conductor, i);
            } else {
                conn.add_cut(r, technology.cuts[*cut].lower);
            }
        }
    }
    conn.build();
    stats.rectangles = conn.rect_count();
    if (diagonal_shapes != 0) {
        stats.warnings.push_back(fmt::format(
            "{} non-Manhattan polygons were approximated by their bounding box", diagonal_shapes));
    }

    // ------------------------------------------------------------------- pins

    // instance -> pin name -> set of nets the labels resolved to
    std::vector<std::map<std::string, std::set<std::size_t>>> instance_pins(flat.instances.size());
    std::map<std::size_t, std::string> net_labels;  // union-find root -> layout name

    for (const gds::FlatText& t : flat.texts) {
        const auto conductor = technology.conductor_of_label(t.layer);
        if (!conductor.has_value() || t.value.empty()) {
            continue;
        }
        ++stats.labels;
        const std::optional<std::size_t> net = conn.net_at(t.position, *conductor);
        if (!net.has_value()) {
            ++stats.unbound_pins;
            stats.warnings.push_back(
                fmt::format("label '{}' at ({}, {}) on {} sits on no conductor shape", t.value,
                            t.position.x, t.position.y, technology.conductors[*conductor].name));
            continue;
        }
        if (t.instance == gds::kNoInstance) {
            // A top-level label names the net for the whole design.
            auto [it, inserted] = net_labels.emplace(*net, t.value);
            if (!inserted && it->second != t.value) {
                stats.warnings.push_back(fmt::format(
                    "net carries conflicting top-level labels '{}' and '{}'", it->second, t.value));
            }
        } else {
            instance_pins[t.instance][t.value].insert(*net);
        }
    }

    // ------------------------------------------------------------- netlist IR

    netlist::Netlist& nl = result.netlist;
    nl.module_name = top.name;

    // Assign supply names from the labels so cell power pins land on them too.
    std::unordered_map<std::size_t, std::size_t> root_to_net;  // uf root -> net index
    auto net_for_root = [&](std::size_t root) -> std::size_t {
        const auto it = root_to_net.find(root);
        if (it != root_to_net.end()) {
            return it->second;
        }
        std::string name;
        netlist::NetKind kind = netlist::NetKind::Signal;
        bool named = false;
        const auto label = net_labels.find(root);
        if (label != net_labels.end()) {
            name = label->second;
            kind = supply_kind(name);
            named = true;
        } else {
            name = fmt::format("{}{}", options.net_prefix, root_to_net.size());
        }
        const std::size_t idx = nl.add_net(name, kind);
        nl.nets[idx].named_in_layout = named;
        root_to_net.emplace(root, idx);
        return idx;
    };

    // Power rails are recognised through the cell power pins as well, because a
    // GDS need not label them at the top level.
    std::unordered_set<std::size_t> power_roots;
    std::unordered_set<std::size_t> ground_roots;
    for (std::size_t i = 0; i < flat.instances.size(); ++i) {
        for (const auto& [pin, nets] : instance_pins[i]) {
            if (nets.size() != 1) {
                continue;
            }
            const std::size_t root = *nets.begin();
            if (pin == "VPWR" || pin == "VPB") {
                power_roots.insert(root);
            } else if (pin == "VGND" || pin == "VNB") {
                ground_roots.insert(root);
            }
        }
    }

    std::unordered_map<std::string, std::size_t> name_counter;

    for (std::size_t i = 0; i < flat.instances.size(); ++i) {
        const gds::Instance& gi = flat.instances[i];
        const tech::CellModel* model = cells.find(gi.cell_name);
        if (model == nullptr) {
            if (std::find(stats.unknown_cells.begin(), stats.unknown_cells.end(), gi.cell_name) ==
                stats.unknown_cells.end()) {
                stats.unknown_cells.push_back(gi.cell_name);
            }
        }

        const bool physical = model != nullptr && model->is_physical();
        if (physical && !options.keep_physical_cells) {
            continue;
        }

        netlist::Instance inst;
        const std::string base = instance_base_name(gi.cell_name);
        inst.name = fmt::format("{}_{}", base, name_counter[base]++);
        inst.cell = gi.cell_name;
        inst.position = gi.transform.origin;
        inst.angle_deg = gi.transform.angle_deg;
        inst.mirror_x = gi.transform.mirror_x;
        const auto abutment = abutments.find(gi.cell_name);
        inst.abutment =
            abutment == abutments.end() ? gi.bbox : transformed_box(abutment->second, gi.transform);

        for (const auto& [pin, nets] : instance_pins[i]) {
            const bool supply = is_supply_name(pin);
            if (supply && !options.keep_power_pins) {
                continue;
            }
            if (nets.size() > 1) {
                ++stats.split_pins;
                stats.warnings.push_back(fmt::format("pin {}.{} resolves to {} distinct nets",
                                                     inst.name, pin, nets.size()));
            }
            inst.pins.push_back(netlist::InstancePin{pin, net_for_root(*nets.begin())});
        }

        // VPB/VNB are labelled on the n-well and the substrate rather than on a
        // routing layer, so no amount of metal tracing reaches them. In this
        // library they are always tied to the cell's own rails; fill them in
        // from VPWR/VGND rather than silently leaving the cell half connected.
        if (options.keep_power_pins && options.infer_body_pins && model != nullptr) {
            const auto net_of = [&](const char* name) -> std::size_t {
                const netlist::InstancePin* p = inst.find(name);
                return p == nullptr ? netlist::kNoNet : p->net;
            };
            const std::pair<const char*, const char*> body_ties[] = {{"VPB", "VPWR"},
                                                                     {"VNB", "VGND"}};
            for (const auto& [body, rail] : body_ties) {
                if (model->find_pin(body) == nullptr || inst.find(body) != nullptr) {
                    continue;
                }
                const std::size_t net = net_of(rail);
                if (net != netlist::kNoNet) {
                    inst.pins.push_back(netlist::InstancePin{body, net});
                    ++stats.inferred_body_pins;
                }
            }
        }

        std::sort(inst.pins.begin(), inst.pins.end(),
                  [](const auto& a, const auto& b) { return a.pin < b.pin; });

        if (!physical) {
            ++stats.logic_instances;
        }
        nl.instances.push_back(std::move(inst));
    }

    // Named nets that no instance touched still deserve to exist (e.g. an
    // output port routed straight to a pad).
    for (const auto& [root, name] : net_labels) {
        net_for_root(root);
    }

    // Classify supplies discovered through cell pins.
    for (auto& [root, idx] : root_to_net) {
        if (nl.nets[idx].kind != netlist::NetKind::Signal) {
            continue;
        }
        if (power_roots.contains(root)) {
            nl.nets[idx].kind = netlist::NetKind::Power;
        } else if (ground_roots.contains(root)) {
            nl.nets[idx].kind = netlist::NetKind::Ground;
        }
    }

    // Net bounding boxes, useful for reports and for stable ordering.
    for (std::size_t h = 0; h < conn.rect_count(); ++h) {
        const auto it = root_to_net.find(conn.net_of_rect(h));
        if (it == root_to_net.end()) {
            continue;
        }
        netlist::Net& net = nl.nets[it->second];
        const Rect& r = conn.rects()[h].rect;
        if (net.bbox == Rect{}) {
            net.bbox = r;
        } else {
            net.bbox.expand(r);
        }
    }

    // ------------------------------------------------------------------ ports

    std::unordered_map<std::size_t, bool> driven;  // net index -> driven by a cell output
    for (const netlist::Instance& inst : nl.instances) {
        const tech::CellModel* model = cells.find(inst.cell);
        if (model == nullptr) {
            continue;
        }
        for (const netlist::InstancePin& p : inst.pins) {
            const tech::Pin* pin = model->find_pin(p.pin);
            if (pin != nullptr && pin->direction == tech::PinDirection::Output) {
                driven[p.net] = true;
            }
        }
    }

    for (std::size_t idx = 0; idx < nl.nets.size(); ++idx) {
        const netlist::Net& net = nl.nets[idx];
        if (!net.named_in_layout) {
            continue;
        }
        netlist::Port port;
        port.name = net.name;
        port.net = idx;
        if (net.kind != netlist::NetKind::Signal) {
            port.direction = netlist::PortDirection::InOut;
        } else {
            port.direction = driven.contains(idx) ? netlist::PortDirection::Output
                                                  : netlist::PortDirection::Input;
        }
        nl.ports.push_back(std::move(port));
    }
    std::sort(nl.ports.begin(), nl.ports.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });

    // ------------------------------------------------------------- geometry

    if (options.keep_geometry) {
        result.geometry.reserve(conn.rect_count());
        for (std::size_t h = 0; h < conn.rect_count(); ++h) {
            const LayerRect& lr = conn.rects()[h];
            const auto it = root_to_net.find(conn.net_of_rect(h));
            result.geometry.push_back(ExtractedRect{
                lr.rect, lr.conductor, it == root_to_net.end() ? netlist::kNoNet : it->second});
        }
        result.instance_boxes.reserve(nl.instances.size());
        for (const netlist::Instance& inst : nl.instances) {
            result.instance_boxes.push_back(inst.abutment);
        }
    }

    stats.nets_total = nl.nets.size();
    stats.nets_signal = static_cast<std::size_t>(
        std::count_if(nl.nets.begin(), nl.nets.end(),
                      [](const netlist::Net& n) { return n.kind == netlist::NetKind::Signal; }));

    return result;
}

}  // namespace asicrev::extract
