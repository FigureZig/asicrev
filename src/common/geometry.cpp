#include "asicrev/common/geometry.hpp"

#include <cmath>

namespace asicrev {

Rect bounding_box(const std::vector<Point>& pts) {
    if (pts.empty()) {
        return Rect{};
    }
    Rect r{pts[0].x, pts[0].y, pts[0].x, pts[0].y};
    for (const Point& p : pts) {
        r.xlo = std::min(r.xlo, p.x);
        r.ylo = std::min(r.ylo, p.y);
        r.xhi = std::max(r.xhi, p.x);
        r.yhi = std::max(r.yhi, p.y);
    }
    return r;
}

namespace {

/// Round to the nearest integer database unit. Layouts are on a grid, so this
/// only ever repairs floating point noise from the rotation.
Dbu round_dbu(double v) {
    return static_cast<Dbu>(std::llround(v));
}

/// Exact sin/cos for the four orientations GDSII layouts actually use.
void sincos_deg(double deg, double& s, double& c) {
    double norm = std::fmod(deg, 360.0);
    if (norm < 0) {
        norm += 360.0;
    }
    if (norm == 0.0) {
        s = 0.0;
        c = 1.0;
    } else if (norm == 90.0) {
        s = 1.0;
        c = 0.0;
    } else if (norm == 180.0) {
        s = 0.0;
        c = -1.0;
    } else if (norm == 270.0) {
        s = -1.0;
        c = 0.0;
    } else {
        const double rad = norm * 3.14159265358979323846 / 180.0;
        s = std::sin(rad);
        c = std::cos(rad);
    }
}

}  // namespace

Point Transform::apply(Point p) const {
    double x = static_cast<double>(p.x) * mag;
    double y = static_cast<double>(p.y) * mag;
    if (mirror_x) {
        y = -y;  // GDSII mirrors about the x axis before rotating
    }
    double s = 0.0;
    double c = 1.0;
    sincos_deg(angle_deg, s, c);
    const double rx = x * c - y * s;
    const double ry = x * s + y * c;
    return Point{origin.x + round_dbu(rx), origin.y + round_dbu(ry)};
}

Transform Transform::compose(const Transform& inner) const {
    Transform out;
    out.origin = apply(inner.origin);
    out.mag = mag * inner.mag;
    // Mirroring about x negates the sense of any rotation applied after it.
    out.angle_deg = mirror_x ? angle_deg - inner.angle_deg : angle_deg + inner.angle_deg;
    out.mirror_x = mirror_x != inner.mirror_x;
    out.angle_deg = std::fmod(out.angle_deg, 360.0);
    if (out.angle_deg < 0) {
        out.angle_deg += 360.0;
    }
    return out;
}

}  // namespace asicrev
