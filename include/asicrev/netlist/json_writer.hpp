#pragma once

#include "asicrev/extract/extractor.hpp"
#include "asicrev/netlist/netlist.hpp"

#include <nlohmann/json.hpp>

namespace asicrev::netlist {

nlohmann::ordered_json to_json(const Netlist& nl);

nlohmann::ordered_json to_json(const extract::ExtractStats& stats);

}  // namespace asicrev::netlist
