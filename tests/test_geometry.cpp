#include "asicrev/common/geometry.hpp"
#include "asicrev/common/union_find.hpp"

#include <doctest/doctest.h>

using namespace asicrev;

TEST_CASE("rectangles touch on a shared edge but do not overlap") {
    const Rect a{0, 0, 10, 10};
    const Rect b{10, 0, 20, 10};
    CHECK(a.touches(b));
    CHECK_FALSE(a.overlaps(b));

    const Rect c{11, 0, 20, 10};
    CHECK_FALSE(a.touches(c));
}

TEST_CASE("transform applies mirror before rotation, GDSII style") {
    Transform xf;
    xf.origin = Point{100, 200};
    CHECK(xf.apply(Point{5, 7}) == Point{105, 207});

    xf.angle_deg = 90.0;
    CHECK(xf.apply(Point{10, 0}) == Point{100, 210});

    Transform mirrored;
    mirrored.mirror_x = true;
    CHECK(mirrored.apply(Point{3, 4}) == Point{3, -4});

    Transform mirror_then_rotate;
    mirror_then_rotate.mirror_x = true;
    mirror_then_rotate.angle_deg = 180.0;  // sky130 "FS" orientation
    CHECK(mirror_then_rotate.apply(Point{3, 4}) == Point{-3, 4});
}

TEST_CASE("composing transforms matches applying them in sequence") {
    Transform outer;
    outer.origin = Point{1000, 2000};
    outer.angle_deg = 90.0;

    Transform inner;
    inner.origin = Point{10, 20};
    inner.mirror_x = true;

    const Transform combined = outer.compose(inner);
    const Point p{7, 3};
    CHECK(combined.apply(p) == outer.apply(inner.apply(p)));
}

TEST_CASE("union-find merges and reports components") {
    UnionFind uf(5);
    CHECK(uf.unite(0, 1));
    CHECK(uf.unite(1, 2));
    CHECK_FALSE(uf.unite(0, 2));
    CHECK(uf.connected(0, 2));
    CHECK_FALSE(uf.connected(0, 3));
}
