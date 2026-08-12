#include "asicrev/netlist/json_writer.hpp"

namespace asicrev::netlist {

namespace {

const char* direction_name(PortDirection d) {
    switch (d) {
        case PortDirection::Input: return "input";
        case PortDirection::Output: return "output";
        case PortDirection::InOut: return "inout";
    }
    return "input";
}

const char* kind_name(NetKind k) {
    switch (k) {
        case NetKind::Signal: return "signal";
        case NetKind::Power: return "power";
        case NetKind::Ground: return "ground";
    }
    return "signal";
}

}  // namespace

nlohmann::ordered_json to_json(const Netlist& nl) {
    nlohmann::ordered_json j;
    j["module"] = nl.module_name;

    auto& ports = j["ports"] = nlohmann::ordered_json::array();
    for (const Port& p : nl.ports) {
        ports.push_back({{"name", p.name},
                         {"direction", direction_name(p.direction)},
                         {"net", p.net == kNoNet ? "" : nl.nets[p.net].name}});
    }

    auto& nets = j["nets"] = nlohmann::ordered_json::array();
    for (const Net& n : nl.nets) {
        nets.push_back({{"name", n.name},
                        {"kind", kind_name(n.kind)},
                        {"labelled", n.named_in_layout},
                        {"bbox", {n.bbox.xlo, n.bbox.ylo, n.bbox.xhi, n.bbox.yhi}}});
    }

    auto& insts = j["instances"] = nlohmann::ordered_json::array();
    for (const Instance& i : nl.instances) {
        nlohmann::ordered_json conns = nlohmann::ordered_json::object();
        for (const InstancePin& p : i.pins) {
            conns[p.pin] = p.net == kNoNet ? "" : nl.nets[p.net].name;
        }
        insts.push_back(
            {{"name", i.name},
             {"cell", i.cell},
             {"x", i.position.x},
             {"y", i.position.y},
             {"abutment", {i.abutment.xlo, i.abutment.ylo, i.abutment.xhi, i.abutment.yhi}},
             {"angle", i.angle_deg},
             {"mirror_x", i.mirror_x},
             {"connections", conns}});
    }
    return j;
}

nlohmann::ordered_json to_json(const extract::ExtractStats& s) {
    nlohmann::ordered_json j;
    j["instances"] = s.instances;
    j["logic_instances"] = s.logic_instances;
    j["polygons"] = s.polygons;
    j["rectangles"] = s.rectangles;
    j["nets_total"] = s.nets_total;
    j["nets_signal"] = s.nets_signal;
    j["labels"] = s.labels;
    j["unbound_pins"] = s.unbound_pins;
    j["split_pins"] = s.split_pins;
    j["inferred_body_pins"] = s.inferred_body_pins;
    j["unknown_cells"] = s.unknown_cells;
    j["warnings"] = s.warnings;
    return j;
}

}  // namespace asicrev::netlist
