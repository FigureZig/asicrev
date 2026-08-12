#include "commands.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <exception>

int main(int argc, char** argv) {
    CLI::App root{"asicrev - reverse engineer a gate-level netlist out of a finished GDSII layout",
                  "asicrev"};
    root.require_subcommand(1);
    root.set_version_flag("--version", std::string("asicrev 0.1.0"));

    asicrev::app::register_inspect(root);
    asicrev::app::register_extract(root);
    asicrev::app::register_compare(root);
    asicrev::app::register_sim(root);
    asicrev::app::register_export(root);

    CLI11_PARSE(root, argc, argv);

    try {
        // Subcommand callbacks do the work; CLI11 already ran them.
        return 0;
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return 1;
    }
}
