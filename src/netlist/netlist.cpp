#include "asicrev/netlist/netlist.hpp"

namespace asicrev::netlist {

const InstancePin* Instance::find(std::string_view pin_name) const {
    for (const InstancePin& p : pins) {
        if (p.pin == pin_name) {
            return &p;
        }
    }
    return nullptr;
}

std::size_t Netlist::add_net(std::string name, NetKind kind) {
    Net n;
    n.name = std::move(name);
    n.kind = kind;
    nets.push_back(std::move(n));
    return nets.size() - 1;
}

std::size_t Netlist::net_by_name(std::string_view name) const {
    for (std::size_t i = 0; i < nets.size(); ++i) {
        if (nets[i].name == name) {
            return i;
        }
    }
    return kNoNet;
}

std::unordered_map<std::string, std::size_t> Netlist::cell_histogram() const {
    std::unordered_map<std::string, std::size_t> hist;
    for (const Instance& i : instances) {
        ++hist[i.cell];
    }
    return hist;
}

}  // namespace asicrev::netlist
