#pragma once

#include "asicrev/gds/library.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace asicrev::gds {

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& what) : std::runtime_error(what) {}
};

/// Read a GDSII stream file into an in-memory library.
///
/// Supports the subset that OpenROAD/KLayout emit for a finished ASIC:
/// BOUNDARY, PATH, SREF, AREF, TEXT and BOX elements with STRANS/MAG/ANGLE.
Library read_gds(const std::filesystem::path& path);

/// Same, from an in-memory buffer (used by the tests).
Library parse_gds(const unsigned char* data, std::size_t size);

}  // namespace asicrev::gds
