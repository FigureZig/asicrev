#include "asicrev/extract/rect_decompose.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <numeric>

using namespace asicrev;
using namespace asicrev::extract;

namespace {

Dbu total_area(const std::vector<Rect>& rects) {
    return std::accumulate(rects.begin(), rects.end(), Dbu{0},
                           [](Dbu acc, const Rect& r) { return acc + r.width() * r.height(); });
}

bool any_overlap(const std::vector<Rect>& rects) {
    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            if (rects[i].overlaps(rects[j])) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST_CASE("a rectangle decomposes to itself") {
    const std::vector<Point> ring = {{0, 0}, {100, 0}, {100, 50}, {0, 50}, {0, 0}};
    const std::vector<Rect> parts = decompose_polygon(ring);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0] == Rect{0, 0, 100, 50});
}

TEST_CASE("an L shape decomposes into disjoint pieces of the right area") {
    // 100x100 square with the top-right 60x60 removed -> area 10000 - 3600.
    const std::vector<Point> ring = {{0, 0},    {100, 0}, {100, 40}, {40, 40},
                                     {40, 100}, {0, 100}, {0, 0}};
    const std::vector<Rect> parts = decompose_polygon(ring);
    CHECK(parts.size() >= 2);
    CHECK(total_area(parts) == 10000 - 3600);
    CHECK_FALSE(any_overlap(parts));
}

TEST_CASE("a U shape keeps both legs separate in the notch slab") {
    //  ____      ____
    // |    |    |    |
    // |    |____|    |
    // |______________|
    const std::vector<Point> ring = {{0, 0},   {100, 0},  {100, 100}, {70, 100}, {70, 40},
                                     {30, 40}, {30, 100}, {0, 100},   {0, 0}};
    const std::vector<Rect> parts = decompose_polygon(ring);
    CHECK(total_area(parts) == 100 * 40 + 2 * (30 * 60));
    CHECK_FALSE(any_overlap(parts));
}

TEST_CASE("a non-Manhattan polygon falls back to its bounding box") {
    const std::vector<Point> ring = {{0, 0}, {100, 30}, {50, 90}, {0, 0}};
    bool diagonal = false;
    const std::vector<Rect> parts = decompose_polygon(ring, &diagonal);
    CHECK(diagonal);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0] == Rect{0, 0, 100, 90});
}

TEST_CASE("adjacent slabs with identical spans are merged") {
    // A plain tall rectangle expressed with redundant vertices must not blow up
    // into one rectangle per y coordinate.
    const std::vector<Point> ring = {{0, 0},   {10, 0}, {10, 50}, {10, 100},
                                     {0, 100}, {0, 50}, {0, 0}};
    const std::vector<Rect> parts = decompose_polygon(ring);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0] == Rect{0, 0, 10, 100});
}
