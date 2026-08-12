#include "asicrev/extract/connectivity.hpp"

#include <doctest/doctest.h>

using namespace asicrev;
using namespace asicrev::extract;

TEST_CASE("touching shapes on one layer form a single net") {
    ConnectivityBuilder c(2, 100);
    const std::size_t a = c.add_conductor(Rect{0, 0, 100, 10}, 0, 0);
    const std::size_t b = c.add_conductor(Rect{100, 0, 200, 10}, 0, 1);
    const std::size_t d = c.add_conductor(Rect{300, 0, 400, 10}, 0, 2);
    c.build();

    CHECK(c.net_of_rect(a) == c.net_of_rect(b));
    CHECK(c.net_of_rect(a) != c.net_of_rect(d));
    CHECK(c.net_count() == 2);
}

TEST_CASE("shapes on different layers stay apart until a cut joins them") {
    ConnectivityBuilder c(2, 100);
    const std::size_t low = c.add_conductor(Rect{0, 0, 100, 100}, 0, 0);
    const std::size_t high = c.add_conductor(Rect{0, 0, 100, 100}, 1, 1);
    c.build();
    CHECK(c.net_of_rect(low) != c.net_of_rect(high));

    ConnectivityBuilder d(2, 100);
    const std::size_t low2 = d.add_conductor(Rect{0, 0, 100, 100}, 0, 0);
    const std::size_t high2 = d.add_conductor(Rect{0, 0, 100, 100}, 1, 1);
    d.add_cut(Rect{40, 40, 60, 60}, 0);
    d.build();
    CHECK(d.net_of_rect(low2) == d.net_of_rect(high2));
}

TEST_CASE("a cut that only touches an edge does not connect") {
    // Cuts must genuinely overlap the metal, otherwise abutting-but-separate
    // wires on adjacent layers would be shorted by a neighbouring via.
    ConnectivityBuilder c(2, 100);
    const std::size_t low = c.add_conductor(Rect{0, 0, 100, 100}, 0, 0);
    const std::size_t high = c.add_conductor(Rect{200, 0, 300, 100}, 1, 1);
    c.add_cut(Rect{100, 40, 200, 60}, 0);
    c.build();
    CHECK(c.net_of_rect(low) != c.net_of_rect(high));
}

TEST_CASE("point queries resolve to the covering shape's net") {
    ConnectivityBuilder c(2, 100);
    c.add_conductor(Rect{0, 0, 100, 100}, 0, 0);
    const std::size_t other = c.add_conductor(Rect{500, 500, 600, 600}, 0, 1);
    c.build();

    const auto hit = c.net_at(Point{50, 50}, 0);
    REQUIRE(hit.has_value());
    CHECK(*hit != c.net_of_rect(other));

    CHECK_FALSE(c.net_at(Point{250, 250}, 0).has_value());
    CHECK_FALSE(c.net_at(Point{50, 50}, 1).has_value());
}

TEST_CASE("a stack of vias carries a net from li1 to met3") {
    ConnectivityBuilder c(4, 100);
    std::vector<std::size_t> handles;
    for (std::size_t layer = 0; layer < 4; ++layer) {
        handles.push_back(c.add_conductor(Rect{0, 0, 50, 50}, layer, layer));
        if (layer + 1 < 4) {
            c.add_cut(Rect{10, 10, 20, 20}, layer);
        }
    }
    c.build();
    for (std::size_t i = 1; i < handles.size(); ++i) {
        CHECK(c.net_of_rect(handles[0]) == c.net_of_rect(handles[i]));
    }
    CHECK(c.net_count() == 1);
}
