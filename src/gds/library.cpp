#include "asicrev/gds/library.hpp"

#include <set>

namespace asicrev::gds {

void Library::reindex() {
    cell_index.clear();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        cell_index[cells[i].name] = i;
    }
}

const Cell* Library::find(const std::string& cell_name) const {
    const auto it = cell_index.find(cell_name);
    return it == cell_index.end() ? nullptr : &cells[it->second];
}

Cell* Library::find(const std::string& cell_name) {
    const auto it = cell_index.find(cell_name);
    return it == cell_index.end() ? nullptr : &cells[it->second];
}

std::vector<const Cell*> Library::top_cells() const {
    std::set<std::string> referenced;
    for (const Cell& c : cells) {
        for (const Reference& r : c.references) {
            referenced.insert(r.cell_name);
        }
    }
    std::vector<const Cell*> tops;
    for (const Cell& c : cells) {
        if (!referenced.contains(c.name)) {
            tops.push_back(&c);
        }
    }
    return tops;
}

}  // namespace asicrev::gds
