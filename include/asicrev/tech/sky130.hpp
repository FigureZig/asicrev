#pragma once

#include "asicrev/gds/library.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace asicrev::tech {

/// Description of the metal/local-interconnect stack we extract connectivity on.
///
/// Conductors are ordered bottom-up; a Cut connects conductor `lower` to
/// conductor `lower + 1`.
struct Conductor {
    std::string name;
    gds::LayerKey drawing;
    gds::LayerKey pin;    ///< pin-purpose shapes, merged into the conductor
    gds::LayerKey label;  ///< text purpose carrying net / pin names
};

struct Cut {
    std::string name;
    gds::LayerKey drawing;
    std::size_t lower;  ///< index into Technology::conductors
};

struct Technology {
    std::string name;
    std::vector<Conductor> conductors;
    std::vector<Cut> cuts;

    /// Index of the conductor a drawing- or pin-purpose layer belongs to.
    std::optional<std::size_t> conductor_of_shape(gds::LayerKey key) const;

    /// Index of the conductor a text label refers to.
    std::optional<std::size_t> conductor_of_label(gds::LayerKey key) const;

    /// Index of the cut layer, if `key` is one.
    std::optional<std::size_t> cut_of_shape(gds::LayerKey key) const;
};

/// The sky130A stack: li1 + met1..met5 with mcon/via/via2/via3/via4.
const Technology& sky130();

}  // namespace asicrev::tech
