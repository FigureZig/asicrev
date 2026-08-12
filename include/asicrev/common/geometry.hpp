#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace asicrev {

/// All coordinates are GDSII database units (integers). sky130 uses 1 dbu = 1 nm.
using Dbu = std::int64_t;

struct Point {
    Dbu x = 0;
    Dbu y = 0;

    friend bool operator==(const Point&, const Point&) = default;
};

/// Axis-aligned rectangle, half-open in neither axis: edges are inclusive, so
/// two rectangles that merely share an edge count as touching (and therefore
/// electrically connected on the same layer).
struct Rect {
    Dbu xlo = 0;
    Dbu ylo = 0;
    Dbu xhi = 0;
    Dbu yhi = 0;

    static Rect from_points(Dbu x0, Dbu y0, Dbu x1, Dbu y1) {
        return Rect{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
    }

    bool empty() const { return xhi <= xlo || yhi <= ylo; }

    Dbu width() const { return xhi - xlo; }

    Dbu height() const { return yhi - ylo; }

    bool contains(Point p) const { return p.x >= xlo && p.x <= xhi && p.y >= ylo && p.y <= yhi; }

    /// True when the rectangles overlap or share an edge/corner.
    bool touches(const Rect& o) const {
        return xlo <= o.xhi && o.xlo <= xhi && ylo <= o.yhi && o.ylo <= yhi;
    }

    /// True when the interiors overlap (strictly positive area intersection).
    bool overlaps(const Rect& o) const {
        return xlo < o.xhi && o.xlo < xhi && ylo < o.yhi && o.ylo < yhi;
    }

    void expand(const Rect& o) {
        xlo = std::min(xlo, o.xlo);
        ylo = std::min(ylo, o.ylo);
        xhi = std::max(xhi, o.xhi);
        yhi = std::max(yhi, o.yhi);
    }

    friend bool operator==(const Rect&, const Rect&) = default;
};

/// Bounding box of a point list.
Rect bounding_box(const std::vector<Point>& pts);

/// A 2D transform as GDSII expresses it: optional mirror about the x axis,
/// then a counter-clockwise rotation, then a translation. Magnification is
/// supported but is 1.0 in every layout we care about.
struct Transform {
    Point origin{0, 0};
    double angle_deg = 0.0;
    bool mirror_x = false;
    double mag = 1.0;

    Point apply(Point p) const;

    /// Composition: `this` applied after `inner` (i.e. parent * child).
    Transform compose(const Transform& inner) const;
};

}  // namespace asicrev
