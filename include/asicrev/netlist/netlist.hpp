#pragma once

#include "asicrev/common/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace asicrev::netlist {

inline constexpr std::size_t kNoNet = std::numeric_limits<std::size_t>::max();

enum class PortDirection : std::uint8_t { Input, Output, InOut };

enum class NetKind : std::uint8_t { Signal, Power, Ground };

struct Net {
    std::string name;
    NetKind kind = NetKind::Signal;
    bool named_in_layout = false;  ///< came from a GDS text label, not auto-numbered
    Rect bbox{};
};

struct InstancePin {
    std::string pin;
    std::size_t net = kNoNet;
};

struct Instance {
    std::string name;
    std::string cell;
    Point position{};  ///< the placed cell's own origin, as stored in the GDS
    /// Abutment box in top-level coordinates. Its lower-left corner is what a
    /// DEF `PLACED` point refers to, which is not the origin for flipped rows.
    Rect abutment{};
    double angle_deg = 0.0;
    bool mirror_x = false;
    std::vector<InstancePin> pins;

    const InstancePin* find(std::string_view pin_name) const;
};

struct Port {
    std::string name;
    PortDirection direction = PortDirection::Input;
    std::size_t net = kNoNet;
};

struct Netlist {
    std::string module_name;
    std::vector<Net> nets;
    std::vector<Instance> instances;
    std::vector<Port> ports;

    std::size_t add_net(std::string name, NetKind kind = NetKind::Signal);
    std::size_t net_by_name(std::string_view name) const;

    std::unordered_map<std::string, std::size_t> cell_histogram() const;
};

}  // namespace asicrev::netlist
