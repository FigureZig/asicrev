#include "asicrev/extract/rect_decompose.hpp"

#include <algorithm>
#include <map>

namespace asicrev::extract {

namespace {

/// Drop the duplicated closing vertex, if present.
std::vector<Point> open_ring(const std::vector<Point>& points) {
    std::vector<Point> ring = points;
    while (ring.size() > 1 && ring.front() == ring.back()) {
        ring.pop_back();
    }
    return ring;
}

struct VEdge {
    Dbu x;
    Dbu ylo;
    Dbu yhi;
};

}  // namespace

bool is_manhattan(const std::vector<Point>& points) {
    const std::vector<Point> ring = open_ring(points);
    if (ring.size() < 4) {
        // A closed rectilinear ring needs at least four vertices, so anything
        // shorter necessarily has a diagonal edge.
        return false;
    }
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Point& a = ring[i];
        const Point& b = ring[(i + 1) % ring.size()];
        if (a.x != b.x && a.y != b.y) {
            return false;
        }
    }
    return true;
}

bool is_rectangle(const std::vector<Point>& points) {
    const std::vector<Point> ring = open_ring(points);
    if (ring.size() != 4) {
        return false;
    }
    std::size_t distinct_x = 0;
    std::size_t distinct_y = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        bool new_x = true;
        bool new_y = true;
        for (std::size_t j = 0; j < i; ++j) {
            new_x = new_x && ring[i].x != ring[j].x;
            new_y = new_y && ring[i].y != ring[j].y;
        }
        distinct_x += new_x ? 1 : 0;
        distinct_y += new_y ? 1 : 0;
    }
    return distinct_x == 2 && distinct_y == 2 && is_manhattan(points);
}

std::vector<Rect> decompose_polygon(const std::vector<Point>& points, bool* had_diagonal) {
    if (had_diagonal != nullptr) {
        *had_diagonal = false;
    }
    const std::vector<Point> ring = open_ring(points);
    if (ring.size() < 3) {
        return {};  // degenerate: a point or a zero-area line
    }

    if (is_rectangle(points)) {
        return {bounding_box(ring)};
    }

    if (!is_manhattan(points)) {
        // Conservative fallback: a diagonal shape becomes its bounding box.
        // This can only over-connect, never lose a connection, and the caller
        // is told so it can report it.
        if (had_diagonal != nullptr) {
            *had_diagonal = true;
        }
        return {bounding_box(ring)};
    }

    // Collect the vertical edges; horizontal ones carry no information for a
    // scanline in y.
    std::vector<VEdge> vedges;
    std::vector<Dbu> ys;
    vedges.reserve(ring.size() / 2);
    ys.reserve(ring.size());
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Point& a = ring[i];
        const Point& b = ring[(i + 1) % ring.size()];
        ys.push_back(a.y);
        if (a.x == b.x && a.y != b.y) {
            vedges.push_back(VEdge{a.x, std::min(a.y, b.y), std::max(a.y, b.y)});
        }
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    if (ys.size() < 2 || vedges.empty()) {
        return {};
    }

    // For each horizontal slab, the vertical edges crossing it, sorted by x,
    // alternate between entering and leaving the polygon interior (even-odd
    // rule). Pairs of consecutive crossings therefore bound the covered spans.
    struct Slab {
        Dbu y0;
        Dbu y1;
        std::vector<std::pair<Dbu, Dbu>> spans;
    };

    std::vector<Slab> slabs;
    slabs.reserve(ys.size() - 1);
    std::vector<Dbu> xs;
    for (std::size_t s = 0; s + 1 < ys.size(); ++s) {
        const Dbu y0 = ys[s];
        const Dbu y1 = ys[s + 1];
        xs.clear();
        for (const VEdge& e : vedges) {
            if (e.ylo <= y0 && e.yhi >= y1) {
                xs.push_back(e.x);
            }
        }
        if (xs.size() < 2) {
            continue;
        }
        std::sort(xs.begin(), xs.end());
        std::vector<std::pair<Dbu, Dbu>> spans;
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            if (xs[i] != xs[i + 1]) {
                spans.emplace_back(xs[i], xs[i + 1]);
            }
        }
        if (!spans.empty()) {
            slabs.push_back(Slab{y0, y1, std::move(spans)});
        }
    }

    // Merge vertically adjacent slabs with identical x spans, so a tall wire
    // does not explode into one rectangle per scanline.
    std::vector<Rect> out;
    std::size_t si = 0;
    while (si < slabs.size()) {
        Dbu y1 = slabs[si].y1;
        std::size_t sj = si + 1;
        while (sj < slabs.size() && slabs[sj].y0 == y1 && slabs[sj].spans == slabs[si].spans) {
            y1 = slabs[sj].y1;
            ++sj;
        }
        for (const auto& [x0, x1] : slabs[si].spans) {
            out.push_back(Rect{x0, slabs[si].y0, x1, y1});
        }
        si = sj;
    }
    return out;
}

}  // namespace asicrev::extract
