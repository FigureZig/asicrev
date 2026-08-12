#!/usr/bin/env bash
#
# Install everything asicrev needs on Ubuntu/Debian.
#
#   ./scripts/bootstrap.sh          build tools only (required)
#   ./scripts/bootstrap.sh --all    also the external checkers (optional)
#
# The external tools are never used to produce a result: they exist so that
# something other than this project can disagree with it.

set -euo pipefail

WANT_EXTRAS=0
[[ "${1:-}" == "--all" ]] && WANT_EXTRAS=1

# Required: a C++20 compiler, CMake 3.24+, and a generator.
REQUIRED=(build-essential cmake ninja-build git pkg-config)

# Optional: yosys gives SAT equivalence checking, iverilog re-runs every
# simulation independently, klayout is a layout viewer for eyeballing a GDS.
EXTRAS=(yosys iverilog klayout clang-format clang-tidy)

say() { printf '\n\033[1m== %s\033[0m\n' "$1"; }

if ! command -v apt-get >/dev/null; then
    echo "This script is for Debian/Ubuntu. On other systems install:" >&2
    printf '  %s\n' "${REQUIRED[@]}" >&2
    exit 1
fi

say "Installing build tools"
sudo apt-get update -qq
sudo apt-get install -y "${REQUIRED[@]}"

if [[ $WANT_EXTRAS -eq 1 ]]; then
    say "Installing optional verification tools"
    sudo apt-get install -y "${EXTRAS[@]}"
fi

say "Versions"
for t in g++ cmake ninja; do
    printf '  %-14s %s\n' "$t" "$($t --version 2>&1 | head -1)"
done
for t in yosys iverilog klayout clang-format; do
    if command -v "$t" >/dev/null; then
        printf '  %-14s %s\n' "$t" "$($t --version 2>&1 | head -1)"
    else
        printf '  %-14s not installed (optional)\n' "$t"
    fi
done

say "Next"
cat <<'EOF'
  cmake --preset default
  cmake --build build/default
  ctest --test-dir build/default

  ./scripts/demo.sh          walk through the whole flow on a real layout
EOF
