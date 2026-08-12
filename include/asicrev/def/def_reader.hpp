#pragma once

#include "asicrev/common/geometry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace asicrev::def {

struct Component {
    std::string name;
    std::string cell;
    Point position{};
    std::string orientation;  ///< N, S, FN, FS, ...
};

struct NetPin {
    std::string instance;  ///< empty for a top-level PIN
    std::string pin;
};

struct Net {
    std::string name;
    std::vector<NetPin> pins;
    bool special = false;
};

struct Pin {
    std::string name;
    std::string direction;
    std::string net;
};

struct Design {
    std::string name;
    Dbu units_per_micron = 1000;
    Rect die_area{};
    std::vector<Component> components;
    std::vector<Net> nets;
    std::vector<Pin> pins;
};

/// Read the parts of a DEF file that describe placement and logical
/// connectivity. Routing geometry is skipped: we only use DEF as ground truth
/// to score the GDS extraction, never as an input to it.
Design read_def(const std::filesystem::path& path);

}  // namespace asicrev::def
