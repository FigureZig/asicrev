# Third-party dependencies.
#
# Everything is pulled in with FetchContent so the project builds from a bare
# checkout with nothing but a compiler and CMake.
#
# Release archives are used rather than git clones: they are an order of
# magnitude smaller, they do not depend on the health of a git server, and each
# one is pinned by SHA-256 so a corrupted or substituted download fails loudly
# instead of producing a mysterious configure error.
#
# Set FETCHCONTENT_FULLY_DISCONNECTED=ON once the _deps cache is populated to
# build offline.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# CMake 4 removed compatibility with `cmake_minimum_required(VERSION < 3.5)`,
# which some of the pinned releases below still declare. Allow them to
# configure rather than forking or patching upstream.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# fmt - formatting for reports and generated Verilog.
FetchContent_Declare(
  fmt
  URL https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.tar.gz
  URL_HASH SHA256=6cb1e6d37bdcb756dbbe59be438790db409cdb4868c66e888d5df9f13f7c027f
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)

# nlohmann/json - machine-readable dump of the extracted netlist IR.
# The release archive is the trimmed one, ~400 kB against ~25 MB for the repo.
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
set(JSON_BuildTests OFF CACHE INTERNAL "")

# CLI11 - subcommand parsing for the driver binary.
FetchContent_Declare(
  CLI11
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz
  URL_HASH SHA256=f2d893a65c3b1324c50d4e682c0cdc021dd0477ae2c048544f39eed6654b699a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
set(CLI11_BUILD_TESTS OFF CACHE INTERNAL "")
set(CLI11_BUILD_EXAMPLES OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(fmt nlohmann_json CLI11)

if(ASICREV_BUILD_TESTS)
  # doctest - unit tests.
  FetchContent_Declare(
    doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
  set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(doctest)
  list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
  set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
endif()
