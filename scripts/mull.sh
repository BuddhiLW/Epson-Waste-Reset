#!/usr/bin/env bash
# Mull (LLVM) mutation testing for the EWR protocol core.
#
# Mutates src/protocol.cpp at the LLVM-IR level and uses tests/property_test.cpp
# as the oracle. Complements the dependency-free scripts/mutation_test.py with a
# much larger, semantic mutant set.
#
# Requires clang-19 and mull-19 (LLVM 19) on the system:
#   Ubuntu: sudo apt-get install clang-19 mull-19
# Override tool locations via env if they live elsewhere:
#   CLANGXX=clang++-19  MULL_PLUGIN=/usr/lib/mull-ir-frontend-19  MULL_RUNNER=mull-runner-19
set -euo pipefail
cd "$(dirname "$0")/.."

CLANGXX="${CLANGXX:-clang++-19}"
PLUGIN="${MULL_PLUGIN:-/usr/lib/mull-ir-frontend-19}"
RUNNER="${MULL_RUNNER:-mull-runner-19}"

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "[*] Instrumenting src/protocol.cpp with the Mull IR pass..."
$CLANGXX -std=c++17 -g -grecord-command-line -Iinclude \
    -fpass-plugin="$PLUGIN" -c src/protocol.cpp -o "$OUT/protocol.o"

echo "[*] Compiling the property-test oracle (not instrumented)..."
$CLANGXX -std=c++17 -g -Iinclude -c tests/property_test.cpp -o "$OUT/oracle.o"
$CLANGXX -g "$OUT/protocol.o" "$OUT/oracle.o" -o "$OUT/mull_property"

echo "[*] Running mull-runner (config: mull.yml)..."
"$RUNNER" --timeout 8000 "$OUT/mull_property"
