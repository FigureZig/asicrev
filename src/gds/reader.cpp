#include "asicrev/gds/reader.hpp"

#include "asicrev/gds/records.hpp"

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string_view>

namespace asicrev::gds {

namespace {

std::uint16_t read_u16(const unsigned char* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
                                      static_cast<std::uint16_t>(p[1]));
}

std::int16_t read_i16(const unsigned char* p) {
    return static_cast<std::int16_t>(read_u16(p));
}

std::int32_t read_i32(const unsigned char* p) {
    const std::uint32_t v =
        (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
        (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    return static_cast<std::int32_t>(v);
}

/// GDSII stores reals in the IBM 360 excess-64 hexadecimal format, not IEEE754:
/// bit 7 is the sign, bits 6..0 the exponent biased by 64 (base 16), and the
/// remaining 7 bytes form the fraction with an implied binary point on the left.
double read_real64(const unsigned char* p) {
    const bool negative = (p[0] & 0x80U) != 0;
    const int exponent = static_cast<int>(p[0] & 0x7FU) - 64;
    std::uint64_t mantissa = 0;
    for (int i = 1; i < 8; ++i) {
        mantissa = (mantissa << 8) | static_cast<std::uint64_t>(p[i]);
    }
    double value = static_cast<double>(mantissa) / static_cast<double>(1ULL << 56);
    value *= std::pow(16.0, static_cast<double>(exponent));
    return negative ? -value : value;
}

std::string read_string(const unsigned char* p, std::size_t n) {
    while (n > 0 && p[n - 1] == '\0') {
        --n;
    }
    return std::string(reinterpret_cast<const char*>(p), n);
}

std::vector<Point> read_points(const unsigned char* p, std::size_t n) {
    std::vector<Point> pts;
    pts.reserve(n / 8);
    for (std::size_t i = 0; i + 8 <= n; i += 8) {
        pts.push_back(Point{read_i32(p + i), read_i32(p + i + 4)});
    }
    return pts;
}

/// Element being assembled between an element-start record and ENDEL.
struct PendingElement {
    enum class Kind { None, Boundary, Path, SRef, ARef, Text, Box, Node } kind = Kind::None;

    LayerKey layer{};
    std::int32_t width = 0;
    std::int16_t path_type = 0;
    std::string sname;
    std::string text;
    std::vector<Point> points;
    Transform transform{};
    std::int16_t columns = 1;
    std::int16_t rows = 1;
};

}  // namespace

Library parse_gds(const unsigned char* data, std::size_t size) {
    Library lib;
    Cell* cell = nullptr;
    PendingElement el;
    bool in_element = false;

    std::size_t pos = 0;
    while (pos + 4 <= size) {
        const std::size_t length = read_u16(data + pos);
        if (length < 4) {
            throw ParseError(fmt::format("record at offset {} has invalid length {}", pos, length));
        }
        if (pos + length > size) {
            throw ParseError(fmt::format("record at offset {} runs past end of file", pos));
        }
        const auto type = static_cast<RecordType>(data[pos + 2]);
        const unsigned char* body = data + pos + 4;
        const std::size_t body_len = length - 4;

        switch (type) {
            case RecordType::LibName: lib.name = read_string(body, body_len); break;

            case RecordType::Units:
                if (body_len >= 16) {
                    lib.user_unit = read_real64(body);
                    lib.db_unit_meters = read_real64(body + 8);
                }
                break;

            case RecordType::BgnStr:
                lib.cells.emplace_back();
                cell = &lib.cells.back();
                break;

            case RecordType::StrName:
                if (cell != nullptr) {
                    cell->name = read_string(body, body_len);
                }
                break;

            case RecordType::EndStr: cell = nullptr; break;

            case RecordType::Boundary:
                el = PendingElement{};
                el.kind = PendingElement::Kind::Boundary;
                in_element = true;
                break;

            case RecordType::Path:
                el = PendingElement{};
                el.kind = PendingElement::Kind::Path;
                in_element = true;
                break;

            case RecordType::SRef:
                el = PendingElement{};
                el.kind = PendingElement::Kind::SRef;
                in_element = true;
                break;

            case RecordType::ARef:
                el = PendingElement{};
                el.kind = PendingElement::Kind::ARef;
                in_element = true;
                break;

            case RecordType::Text:
                el = PendingElement{};
                el.kind = PendingElement::Kind::Text;
                in_element = true;
                break;

            case RecordType::Box:
                el = PendingElement{};
                el.kind = PendingElement::Kind::Box;
                in_element = true;
                break;

            case RecordType::Node:
                el = PendingElement{};
                el.kind = PendingElement::Kind::Node;
                in_element = true;
                break;

            case RecordType::Layer:
                if (body_len >= 2) {
                    el.layer.layer = read_i16(body);
                }
                break;

            case RecordType::DataType:
            case RecordType::TextType:
            case RecordType::BoxType:
            case RecordType::NodeType:
                if (body_len >= 2) {
                    el.layer.datatype = read_i16(body);
                }
                break;

            case RecordType::Width:
                if (body_len >= 4) {
                    el.width = read_i32(body);
                }
                break;

            case RecordType::PathType:
                if (body_len >= 2) {
                    el.path_type = read_i16(body);
                }
                break;

            case RecordType::SName: el.sname = read_string(body, body_len); break;

            case RecordType::String: el.text = read_string(body, body_len); break;

            case RecordType::STrans:
                if (body_len >= 2) {
                    const std::uint16_t flags = read_u16(body);
                    el.transform.mirror_x = (flags & 0x8000U) != 0;
                }
                break;

            case RecordType::Mag:
                if (body_len >= 8) {
                    el.transform.mag = read_real64(body);
                }
                break;

            case RecordType::Angle:
                if (body_len >= 8) {
                    el.transform.angle_deg = read_real64(body);
                }
                break;

            case RecordType::ColRow:
                if (body_len >= 4) {
                    el.columns = read_i16(body);
                    el.rows = read_i16(body + 2);
                }
                break;

            case RecordType::XY: el.points = read_points(body, body_len); break;

            case RecordType::EndEl: {
                if (cell == nullptr || !in_element) {
                    in_element = false;
                    break;
                }
                switch (el.kind) {
                    case PendingElement::Kind::Boundary:
                        cell->boundaries.push_back(Boundary{el.layer, std::move(el.points)});
                        break;

                    case PendingElement::Kind::Box:
                        // A BOX is just a rectangle; treat it as a boundary.
                        cell->boundaries.push_back(Boundary{el.layer, std::move(el.points)});
                        break;

                    case PendingElement::Kind::Path:
                        cell->paths.push_back(
                            Path{el.layer, el.width, el.path_type, std::move(el.points)});
                        break;

                    case PendingElement::Kind::Text: {
                        Text t;
                        t.layer = el.layer;
                        t.position = el.points.empty() ? Point{} : el.points.front();
                        t.value = std::move(el.text);
                        t.transform = el.transform;
                        cell->texts.push_back(std::move(t));
                        break;
                    }

                    case PendingElement::Kind::SRef: {
                        Reference r;
                        r.cell_name = std::move(el.sname);
                        r.transform = el.transform;
                        r.transform.origin = el.points.empty() ? Point{} : el.points.front();
                        cell->references.push_back(std::move(r));
                        break;
                    }

                    case PendingElement::Kind::ARef: {
                        // XY holds the array anchor plus the far corners of the
                        // column and row axes, scaled by the repeat counts.
                        Reference r;
                        r.cell_name = std::move(el.sname);
                        r.transform = el.transform;
                        r.columns = el.columns;
                        r.rows = el.rows;
                        if (el.points.size() >= 3) {
                            const Point anchor = el.points[0];
                            const Point col_end = el.points[1];
                            const Point row_end = el.points[2];
                            r.transform.origin = anchor;
                            const auto cols = el.columns > 0 ? el.columns : 1;
                            const auto rws = el.rows > 0 ? el.rows : 1;
                            r.col_step =
                                Point{(col_end.x - anchor.x) / cols, (col_end.y - anchor.y) / cols};
                            r.row_step =
                                Point{(row_end.x - anchor.x) / rws, (row_end.y - anchor.y) / rws};
                        }
                        cell->references.push_back(std::move(r));
                        break;
                    }

                    case PendingElement::Kind::Node:
                    case PendingElement::Kind::None: break;
                }
                in_element = false;
                el = PendingElement{};
                break;
            }

            case RecordType::EndLib:
                pos = size;  // stop after ENDLIB
                continue;

            default: break;  // headers, properties and other metadata we do not need
        }

        pos += length;
    }

    lib.reindex();
    return lib;
}

Library read_gds(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ParseError(fmt::format("cannot open GDS file '{}'", path.string()));
    }
    std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
    if (buffer.size() < 4) {
        throw ParseError(fmt::format("'{}' is too small to be a GDSII stream", path.string()));
    }
    return parse_gds(buffer.data(), buffer.size());
}

}  // namespace asicrev::gds
