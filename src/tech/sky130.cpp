#include "asicrev/tech/sky130.hpp"

namespace asicrev::tech {

std::optional<std::size_t> Technology::conductor_of_shape(gds::LayerKey key) const {
    for (std::size_t i = 0; i < conductors.size(); ++i) {
        if (conductors[i].drawing == key || conductors[i].pin == key) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> Technology::conductor_of_label(gds::LayerKey key) const {
    for (std::size_t i = 0; i < conductors.size(); ++i) {
        if (conductors[i].label == key) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> Technology::cut_of_shape(gds::LayerKey key) const {
    for (std::size_t i = 0; i < cuts.size(); ++i) {
        if (cuts[i].drawing == key) {
            return i;
        }
    }
    return std::nullopt;
}

const Technology& sky130() {
    // GDS layer numbers from the sky130A layer map (skywater-pdk
    // sky130_fd_sc_hd). Purposes: 20 = drawing, 16 = pin, 5 = label, 44 = via.
    //
    // Below li1 the stack continues into licon1/poly/diff, but we deliberately
    // stop at li1: standard cells are treated as black boxes whose behaviour
    // comes from the cell library, so transistor-level geometry is irrelevant.
    static const Technology tech = [] {
        Technology t;
        t.name = "sky130A";
        t.conductors = {
            Conductor{"li1", {67, 20}, {67, 16}, {67, 5}},
            Conductor{"met1", {68, 20}, {68, 16}, {68, 5}},
            Conductor{"met2", {69, 20}, {69, 16}, {69, 5}},
            Conductor{"met3", {70, 20}, {70, 16}, {70, 5}},
            Conductor{"met4", {71, 20}, {71, 16}, {71, 5}},
            Conductor{"met5", {72, 20}, {72, 16}, {72, 5}},
        };
        t.cuts = {
            Cut{"mcon", {67, 44}, 0},  // li1  -> met1
            Cut{"via", {68, 44}, 1},   // met1 -> met2
            Cut{"via2", {69, 44}, 2},  // met2 -> met3
            Cut{"via3", {70, 44}, 3},  // met3 -> met4
            Cut{"via4", {71, 44}, 4},  // met4 -> met5
        };
        return t;
    }();
    return tech;
}

}  // namespace asicrev::tech
