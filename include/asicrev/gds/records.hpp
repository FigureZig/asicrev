#pragma once

#include <cstdint>

namespace asicrev::gds {

/// GDSII stream record types (Calma GDSII Stream Format, release 6.0).
enum class RecordType : std::uint8_t {
    Header = 0x00,
    BgnLib = 0x01,
    LibName = 0x02,
    Units = 0x03,
    EndLib = 0x04,
    BgnStr = 0x05,
    StrName = 0x06,
    EndStr = 0x07,
    Boundary = 0x08,
    Path = 0x09,
    SRef = 0x0A,
    ARef = 0x0B,
    Text = 0x0C,
    Layer = 0x0D,
    DataType = 0x0E,
    Width = 0x0F,
    XY = 0x10,
    EndEl = 0x11,
    SName = 0x12,
    ColRow = 0x13,
    Node = 0x15,
    TextType = 0x16,
    Presentation = 0x17,
    String = 0x19,
    STrans = 0x1A,
    Mag = 0x1B,
    Angle = 0x1C,
    RefLibs = 0x1F,
    Fonts = 0x20,
    PathType = 0x21,
    Generations = 0x22,
    AttrTable = 0x23,
    ElfFlags = 0x26,
    NodeType = 0x2A,
    PropAttr = 0x2B,
    PropValue = 0x2C,
    Box = 0x2D,
    BoxType = 0x2E,
    Plex = 0x2F,
    BgnExtn = 0x30,
    EndExtn = 0x31,
};

}  // namespace asicrev::gds
