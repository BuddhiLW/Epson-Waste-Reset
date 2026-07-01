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

# clang picks the newest GCC install dir it finds, which on some boxes is a
# headerless gcc-14 stub -> "'cstdint' file not found". Pin an explicit libstdc++
# toolchain when one is provided (or auto-detect the newest that ships headers).
GCC_DIR="${MULL_GCC_INSTALL_DIR:-}"
if [ -z "$GCC_DIR" ]; then
    for d in $(ls -d /usr/lib/gcc/x86_64-linux-gnu/* 2>/dev/null | sort -Vr); do
        ver="$(basename "$d")"
        if [ -d "/usr/include/c++/$ver" ]; then GCC_DIR="$d"; break; fi
    done
fi
GCC_FLAG=""
[ -n "$GCC_DIR" ] && GCC_FLAG="--gcc-install-dir=$GCC_DIR" && echo "[*] Using libstdc++ from $GCC_DIR"

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "[*] Instrumenting src/protocol.cpp with the Mull IR pass..."
$CLANGXX -std=c++17 $GCC_FLAG -g -grecord-command-line -Iinclude \
    -fpass-plugin="$PLUGIN" -c src/protocol.cpp -o "$OUT/protocol.o"

echo "[*] Compiling the property-test oracle (not instrumented)..."
$CLANGXX -std=c++17 $GCC_FLAG -g -Iinclude -c tests/property_test.cpp -o "$OUT/oracle.o"
$CLANGXX $GCC_FLAG -g "$OUT/protocol.o" "$OUT/oracle.o" -o "$OUT/mull_property"

echo "[*] Running mull-runner (config: mull.yml)..."
"$RUNNER" --timeout 8000 "$OUT/mull_property"
