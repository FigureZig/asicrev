cmake_minimum_required(VERSION 3.24)

project(
  asicrev
  VERSION 0.1.0
  DESCRIPTION "GDSII -> netlist -> Verilog reverse-engineering pipeline (sky130)"
  LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      RelWithDebInfo
      CACHE STRING "" FORCE)
endif()

option(ASICREV_BUILD_TESTS "Build unit tests" ON)
option(ASICREV_ENABLE_WARNINGS "Enable a strict warning set" ON)
option(ASICREV_ENABLE_SANITIZERS "Enable ASan/UBSan in Debug builds" OFF)

include(CompilerWarnings)
include(Dependencies)

# ---------------------------------------------------------------- core library

add_library(
  asicrev_core
  src/common/geometry.cpp
  src/gds/reader.cpp
  src/gds/library.cpp
  src/gds/flatten.cpp
  src/tech/sky130.cpp
  src/tech/std_cells.cpp
  src/extract/rect_decompose.cpp
  src/extract/connectivity.cpp
  src/extract/extractor.cpp
  src/netlist/netlist.cpp
  src/netlist/verilog_writer.cpp
  src/netlist/verilog_reader.cpp
  src/netlist/compare.cpp
  src/netlist/json_writer.cpp
  src/export/svg_writer.cpp
  src/def/def_reader.cpp
  src/sim/simulator.cpp
  src/sim/vcd_writer.cpp)

target_include_directories(asicrev_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(asicrev_core PUBLIC fmt::fmt nlohmann_json::nlohmann_json)
asicrev_set_warnings(asicrev_core)
asicrev_set_sanitizers(asicrev_core)

# ----------------------------------------------------------------- application

add_executable(
  asicrev
  apps/main.cpp
  apps/cmd_inspect.cpp
  apps/cmd_extract.cpp
  apps/cmd_compare.cpp
  apps/cmd_sim.cpp
  apps/cmd_export.cpp)
target_link_libraries(asicrev PRIVATE asicrev_core CLI11::CLI11)
asicrev_set_warnings(asicrev)
asicrev_set_sanitizers(asicrev)

# ----------------------------------------------------------------------- tests

if(ASICREV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --------------------------------------------------------------- format target

find_program(ASICREV_CLANG_FORMAT NAMES clang-format clang-format-21 clang-format-18)
if(ASICREV_CLANG_FORMAT)
  file(
    GLOB_RECURSE ASICREV_FORMAT_SOURCES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    include/*.hpp src/*.cpp src/*.hpp apps/*.cpp apps/*.hpp tests/*.cpp)
  add_custom_target(
    format
    COMMAND ${ASICREV_CLANG_FORMAT} -i --style=file ${ASICREV_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running clang-format over all sources")
  add_custom_target(
    format-check
    COMMAND ${ASICREV_CLANG_FORMAT} --dry-run --Werror --style=file
            ${ASICREV_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking formatting")
endif()
