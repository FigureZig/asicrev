#include "asicrev/gds/flatten.hpp"
#include "asicrev/gds/reader.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <vector>

using namespace asicrev;

namespace {

/// Tiny GDSII stream builder, so the reader can be tested without any fixture
/// file on disk.
struct GdsBuilder {
    std::vector<unsigned char> data;

    void record(std::uint8_t type, std::uint8_t dtype, const std::vector<unsigned char>& body) {
        const std::size_t len = body.size() + 4;
        data.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
        data.push_back(static_cast<unsigned char>(len & 0xFF));
        data.push_back(type);
        data.push_back(dtype);
        data.insert(data.end(), body.begin(), body.end());
    }

    static std::vector<unsigned char> i16(std::int16_t v) {
        return {static_cast<unsigned char>((v >> 8) & 0xFF), static_cast<unsigned char>(v & 0xFF)};
    }

    static std::vector<unsigned char> i32(std::int32_t v) {
        return {static_cast<unsigned char>((v >> 24) & 0xFF),
                static_cast<unsigned char>((v >> 16) & 0xFF),
                static_cast<unsigned char>((v >> 8) & 0xFF), static_cast<unsigned char>(v & 0xFF)};
    }

    static std::vector<unsigned char> str(const std::string& s) {
        std::vector<unsigned char> b(s.begin(), s.end());
        if (b.size() % 2 != 0) {
            b.push_back(0);
        }
        return b;
    }

    static std::vector<unsigned char> xy(const std::vector<Point>& pts) {
        std::vector<unsigned char> b;
        for (const Point& p : pts) {
            const auto x = i32(static_cast<std::int32_t>(p.x));
            const auto y = i32(static_cast<std::int32_t>(p.y));
            b.insert(b.end(), x.begin(), x.end());
            b.insert(b.end(), y.begin(), y.end());
        }
        return b;
    }

    /// 1e-6 user unit / 1e-9 db unit as IBM excess-64 reals.
    static std::vector<unsigned char> units() {
        // 1e-3 == 0x3A41893755BC4C0F approximately; the reader only needs the
        // exponent handling to be right, so use exact powers of 16 instead.
        // 1/16 -> exponent 0, mantissa 0x10000000000000
        std::vector<unsigned char> b = {0x40, 0x10, 0, 0, 0, 0, 0, 0,   // 1/16
                                        0x3F, 0x10, 0, 0, 0, 0, 0, 0};  // 1/256
        return b;
    }

    void boundary(std::int16_t layer, std::int16_t dtype, const std::vector<Point>& pts) {
        record(0x08, 0x00, {});
        record(0x0D, 0x02, i16(layer));
        record(0x0E, 0x02, i16(dtype));
        record(0x10, 0x03, xy(pts));
        record(0x11, 0x00, {});
    }

    void text(std::int16_t layer, std::int16_t ttype, Point at, const std::string& value) {
        record(0x0C, 0x00, {});
        record(0x0D, 0x02, i16(layer));
        record(0x16, 0x02, i16(ttype));
        record(0x10, 0x03, xy({at}));
        record(0x19, 0x06, str(value));
        record(0x11, 0x00, {});
    }

    void sref(const std::string& cell, Point at, bool mirror = false, double angle = 0.0) {
        record(0x0A, 0x00, {});
        record(0x12, 0x06, str(cell));
        if (mirror) {
            record(0x1A, 0x01, {0x80, 0x00});
        }
        if (angle == 90.0) {
            // 90.0 == 0x42 5A 00 00 00 00 00 00 (5.625 * 16^1)
            record(0x1C, 0x05, {0x42, 0x5A, 0, 0, 0, 0, 0, 0});
        }
        record(0x10, 0x03, xy({at}));
        record(0x11, 0x00, {});
    }

    void begin_cell(const std::string& name) {
        record(0x05, 0x02, std::vector<unsigned char>(24, 0));
        record(0x06, 0x06, str(name));
    }

    void end_cell() { record(0x07, 0x00, {}); }

    void finish() { record(0x04, 0x00, {}); }
};

}  // namespace

TEST_CASE("reader parses cells, geometry, labels and references") {
    GdsBuilder b;
    b.record(0x00, 0x02, GdsBuilder::i16(600));
    b.record(0x01, 0x02, std::vector<unsigned char>(24, 0));
    b.record(0x02, 0x06, GdsBuilder::str("TESTLIB"));
    b.record(0x03, 0x05, GdsBuilder::units());

    b.begin_cell("leaf");
    b.boundary(67, 20, {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 0}});
    b.text(67, 5, Point{50, 50}, "A");
    b.end_cell();

    b.begin_cell("top");
    b.sref("leaf", Point{1000, 2000});
    b.sref("leaf", Point{5000, 2000}, /*mirror=*/true);
    b.boundary(68, 20, {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}});
    b.end_cell();
    b.finish();

    const gds::Library lib = gds::parse_gds(b.data.data(), b.data.size());

    CHECK(lib.name == "TESTLIB");
    CHECK(lib.cells.size() == 2);

    const gds::Cell* leaf = lib.find("leaf");
    REQUIRE(leaf != nullptr);
    REQUIRE(leaf->boundaries.size() == 1);
    CHECK(leaf->boundaries[0].layer == gds::LayerKey{67, 20});
    REQUIRE(leaf->texts.size() == 1);
    CHECK(leaf->texts[0].value == "A");
    CHECK(leaf->texts[0].layer == gds::LayerKey{67, 5});

    const std::vector<const gds::Cell*> tops = lib.top_cells();
    REQUIRE(tops.size() == 1);
    CHECK(tops[0]->name == "top");

    SUBCASE("flattening applies the placement transforms") {
        const gds::FlatLayout flat =
            gds::flatten(lib, *tops[0], [](const std::string& n) { return n == "leaf"; });
        REQUIRE(flat.instances.size() == 2);
        CHECK(flat.instances[0].transform.origin == Point{1000, 2000});
        CHECK(flat.instances[1].transform.mirror_x);

        // Two leaf labels plus the top-level metal shape.
        CHECK(flat.texts.size() == 2);
        CHECK(flat.texts[0].position == Point{1050, 2050});
        CHECK(flat.texts[1].position == Point{5050, 1950});  // mirrored about x
        CHECK(flat.polygons.size() == 3);
    }
}

TEST_CASE("reader rejects a truncated stream") {
    GdsBuilder b;
    b.record(0x00, 0x02, GdsBuilder::i16(600));
    b.data.push_back(0xFF);  // claim a huge record
    b.data.push_back(0xFF);
    b.data.push_back(0x06);
    b.data.push_back(0x06);
    CHECK_THROWS_AS(gds::parse_gds(b.data.data(), b.data.size()), gds::ParseError);
}
