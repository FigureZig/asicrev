#include "asicrev/gds/flatten.hpp"

#include "asicrev/gds/reader.hpp"

#include <fmt/format.h>

#include <unordered_set>

namespace asicrev::gds {

namespace {

/// Convert a PATH element into a polygon so downstream code only ever sees
/// boundaries. Only Manhattan segments are supported, which is all a routed
/// design contains; `pathtype 2` extends the ends by half the width.
void path_to_polygons(const Path& p, std::vector<Boundary>& out) {
    if (p.points.size() < 2 || p.width <= 0) {
        return;
    }
    const Dbu half = p.width / 2;
    const bool extend = p.path_type == 2 || p.path_type == 1;
    for (std::size_t i = 0; i + 1 < p.points.size(); ++i) {
        Point a = p.points[i];
        Point b = p.points[i + 1];
        Rect r;
        if (a.y == b.y) {
            const Dbu x0 = std::min(a.x, b.x) - (extend ? half : 0);
            const Dbu x1 = std::max(a.x, b.x) + (extend ? half : 0);
            r = Rect{x0, a.y - half, x1, a.y + half};
        } else if (a.x == b.x) {
            const Dbu y0 = std::min(a.y, b.y) - (extend ? half : 0);
            const Dbu y1 = std::max(a.y, b.y) + (extend ? half : 0);
            r = Rect{a.x - half, y0, a.x + half, y1};
        } else {
            const Rect bb = bounding_box({a, b});
            r = Rect{bb.xlo - half, bb.ylo - half, bb.xhi + half, bb.yhi + half};
        }
        out.push_back(Boundary{p.layer,
                               {Point{r.xlo, r.ylo}, Point{r.xhi, r.ylo}, Point{r.xhi, r.yhi},
                                Point{r.xlo, r.yhi}, Point{r.xlo, r.ylo}}});
    }
}

struct Flattener {
    const Library& lib;
    const LeafPredicate& is_leaf;
    FlatLayout out;
    std::unordered_set<std::string> active;  ///< cycle guard
    std::vector<std::string> missing;

    void emit_cell(const Cell& cell, const Transform& xf, std::size_t instance,
                   const std::string& path) {
        std::vector<Boundary> converted;
        for (const Path& p : cell.paths) {
            path_to_polygons(p, converted);
        }

        auto emit_boundary = [&](const Boundary& b) {
            FlatPolygon fp;
            fp.layer = b.layer;
            fp.instance = instance;
            fp.points.reserve(b.points.size());
            for (const Point& pt : b.points) {
                fp.points.push_back(xf.apply(pt));
            }
            out.polygons.push_back(std::move(fp));
        };

        for (const Boundary& b : cell.boundaries) {
            emit_boundary(b);
        }
        for (const Boundary& b : converted) {
            emit_boundary(b);
        }
        for (const Text& t : cell.texts) {
            out.texts.push_back(FlatText{t.layer, xf.apply(t.position), t.value, instance});
        }

        for (const Reference& r : cell.references) {
            const Cell* child = lib.find(r.cell_name);
            if (child == nullptr) {
                missing.push_back(r.cell_name);
                continue;
            }
            for (std::int16_t row = 0; row < std::max<std::int16_t>(r.rows, 1); ++row) {
                for (std::int16_t col = 0; col < std::max<std::int16_t>(r.columns, 1); ++col) {
                    Transform local = r.transform;
                    local.origin.x += r.col_step.x * col + r.row_step.x * row;
                    local.origin.y += r.col_step.y * col + r.row_step.y * row;
                    const Transform child_xf = xf.compose(local);

                    std::string child_path = path + "/" + r.cell_name;
                    if (r.is_array()) {
                        child_path += fmt::format("[{}][{}]", row, col);
                    }

                    if (is_leaf && is_leaf(r.cell_name)) {
                        const std::size_t idx = out.instances.size();
                        out.instances.push_back(
                            Instance{r.cell_name, child_path, child_xf, Rect{}});
                        const std::size_t first_poly = out.polygons.size();
                        descend(*child, child_xf, idx, child_path);
                        Rect bb{};
                        bool have = false;
                        for (std::size_t i = first_poly; i < out.polygons.size(); ++i) {
                            const Rect pb = bounding_box(out.polygons[i].points);
                            if (!have) {
                                bb = pb;
                                have = true;
                            } else {
                                bb.expand(pb);
                            }
                        }
                        out.instances[idx].bbox = bb;
                    } else {
                        descend(*child, child_xf, instance, child_path);
                    }
                }
            }
        }
    }

    void descend(const Cell& cell, const Transform& xf, std::size_t instance,
                 const std::string& path) {
        if (!active.insert(cell.name).second) {
            throw ParseError(fmt::format("cyclic hierarchy at cell '{}'", cell.name));
        }
        emit_cell(cell, xf, instance, path);
        active.erase(cell.name);
    }
};

}  // namespace

FlatLayout flatten(const Library& lib, const Cell& top, const LeafPredicate& is_leaf) {
    Flattener f{lib, is_leaf, {}, {}, {}};
    f.descend(top, Transform{}, kNoInstance, top.name);

    FlatLayout result = std::move(f.out);
    bool have = false;
    for (const FlatPolygon& p : result.polygons) {
        const Rect bb = bounding_box(p.points);
        if (!have) {
            result.bbox = bb;
            have = true;
        } else {
            result.bbox.expand(bb);
        }
    }
    return result;
}

}  // namespace asicrev::gds
