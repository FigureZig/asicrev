#pragma once

#include "asicrev/gds/library.hpp"

#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace asicrev::gds {

inline constexpr std::size_t kNoInstance = std::numeric_limits<std::size_t>::max();

/// One placed leaf cell after hierarchy traversal.
struct Instance {
    std::string cell_name;
    std::string path;  ///< hierarchical path, e.g. "top/sub[3]"
    Transform transform;
    Rect bbox;
};

/// A polygon after transformation into top-level coordinates.
struct FlatPolygon {
    LayerKey layer;
    std::vector<Point> points;
    std::size_t instance = kNoInstance;  ///< owning placed cell, or kNoInstance for top-level
};

struct FlatText {
    LayerKey layer;
    Point position;
    std::string value;
    std::size_t instance = kNoInstance;
};

struct FlatLayout {
    std::vector<Instance> instances;
    std::vector<FlatPolygon> polygons;
    std::vector<FlatText> texts;
    Rect bbox{};
};

/// Predicate deciding whether a referenced cell should be treated as an opaque
/// leaf (its contents still get flattened, but it is recorded as an Instance).
using LeafPredicate = std::function<bool(const std::string& cell_name)>;

/// Flatten `top` into absolute coordinates.
///
/// Cells for which `is_leaf` returns true become entries in `instances`, and
/// every shape underneath them is tagged with that instance index. Everything
/// else is inlined with `instance == kNoInstance`.
FlatLayout flatten(const Library& lib, const Cell& top, const LeafPredicate& is_leaf);

}  // namespace asicrev::gds
