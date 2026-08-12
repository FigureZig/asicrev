#pragma once

#include "asicrev/common/geometry.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace asicrev::gds {

/// A (layer number, datatype) pair, the GDSII notion of "which mask layer".
struct LayerKey {
    std::int16_t layer = 0;
    std::int16_t datatype = 0;

    friend auto operator<=>(const LayerKey&, const LayerKey&) = default;
    friend bool operator==(const LayerKey&, const LayerKey&) = default;
};

struct Boundary {
    LayerKey layer;
    std::vector<Point> points;  ///< closed ring; first point repeated at the end
};

struct Path {
    LayerKey layer;
    std::int32_t width = 0;
    std::int16_t path_type = 0;
    std::vector<Point> points;
};

struct Text {
    LayerKey layer;  ///< datatype field holds TEXTTYPE
    Point position;
    std::string value;
    Transform transform;
};

/// Structure reference. A plain SREF is an ARef with a 1x1 array.
struct Reference {
    std::string cell_name;
    Transform transform;
    std::int16_t columns = 1;
    std::int16_t rows = 1;
    Point col_step{0, 0};
    Point row_step{0, 0};

    bool is_array() const { return columns > 1 || rows > 1; }
};

struct Cell {
    std::string name;
    std::vector<Boundary> boundaries;
    std::vector<Path> paths;
    std::vector<Text> texts;
    std::vector<Reference> references;
};

struct Library {
    std::string name;
    double user_unit = 1e-6;       ///< metres per user unit
    double db_unit_meters = 1e-9;  ///< metres per database unit
    std::vector<Cell> cells;
    std::map<std::string, std::size_t> cell_index;

    const Cell* find(const std::string& name) const;
    Cell* find(const std::string& name);

    /// Cells that are never referenced by another cell.
    std::vector<const Cell*> top_cells() const;

    void reindex();
};

}  // namespace asicrev::gds
