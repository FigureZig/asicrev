#pragma once

#include "asicrev/common/geometry.hpp"

#include <vector>

namespace asicrev::extract {

/// Decompose a rectilinear (Manhattan) polygon into non-overlapping rectangles.
///
/// A scanline over the distinct y coordinates produces one horizontal slab per
/// gap; within a slab the polygon's vertical edges are crossed in x order and
/// paired up by the even-odd rule. Feeding a plain rectangle through returns it
/// unchanged, and self-touching rings (common for li1 pin shapes) are handled
/// correctly because the pairing is done per slab.
///
/// Non-Manhattan polygons fall back to their bounding box, which is
/// conservative for connectivity; `had_diagonal` reports when that happened.
std::vector<Rect> decompose_polygon(const std::vector<Point>& points, bool* had_diagonal = nullptr);

/// True when every edge of the ring is axis aligned.
bool is_manhattan(const std::vector<Point>& points);

/// True for a 4- or 5-point axis-aligned rectangle ring.
bool is_rectangle(const std::vector<Point>& points);

}  // namespace asicrev::extract
