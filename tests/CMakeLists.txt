add_executable(
  asicrev_tests
  test_main.cpp
  test_geometry.cpp
  test_rect_decompose.cpp
  test_connectivity.cpp
  test_gds_reader.cpp
  test_std_cells.cpp
  test_verilog_roundtrip.cpp
  test_compare.cpp
  test_simulator.cpp
  test_warmup_e2e.cpp
  test_svg_writer.cpp)

target_link_libraries(asicrev_tests PRIVATE asicrev_core doctest::doctest)
asicrev_set_warnings(asicrev_tests)
asicrev_set_sanitizers(asicrev_tests)

target_compile_definitions(
  asicrev_tests PRIVATE ASICREV_WARMUP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../external/janestreet-asic-puzzle/warmup")

include(doctest)
doctest_discover_tests(asicrev_tests)
