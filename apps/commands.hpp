#pragma once

#include <CLI/CLI.hpp>

namespace asicrev::app {

void register_inspect(CLI::App& root);
void register_extract(CLI::App& root);
void register_compare(CLI::App& root);
void register_sim(CLI::App& root);
void register_export(CLI::App& root);

}  // namespace asicrev::app
