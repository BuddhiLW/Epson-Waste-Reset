#!/usr/bin/env bash
# Diff the refactored core's emitted bytes against upstream's OWN generator
# (fetched from the upstream remote) over every model. Exit 0 == identical.
#
# Usage: tests/verify_against_upstream.sh [UPSTREAM_REF] [DATABASE_JSON]
#   UPSTREAM_REF   git ref in the upstream remote (default: main)
#   DATABASE_JSON  model list to compare over     (default: ./database.json)
# Requires: git, g++ (C++17), libcurl dev headers. Run from the repo root.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

UPSTREAM_REF="${1:-main}"
DB="${2:-$REPO_ROOT/database.json}"
UPSTREAM_URL="https://github.com/RxNaison/Epson-Waste-Reset.git"

# Ensure an 'upstream' remote pointing at the canonical repo, then fetch it.
if ! git remote get-url upstream >/dev/null 2>&1; then
    git remote add upstream "$UPSTREAM_URL"
fi
echo "Oracle remote : $(git remote get-url upstream)"
echo "Oracle ref    : $UPSTREAM_REF"
echo "Database      : $DB"
git fetch --quiet upstream "$UPSTREAM_REF"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/upstream/ewr"

# Extract upstream source verbatim from the fetched ref.
git show "upstream/${UPSTREAM_REF}:include/ewr/generator.h" > "$WORK/upstream/ewr/generator.h"
git show "upstream/${UPSTREAM_REF}:src/generator.cpp"       > "$WORK/upstream/generator.cpp"
echo "Upstream generator.cpp sha256: $(sha256sum "$WORK/upstream/generator.cpp" | cut -d' ' -f1)"

CXX="${CXX:-g++}"
STD="-std=c++17 -O2"

# 1) Upstream oracle dumper — built ONLY from upstream's own generator.
"$CXX" $STD \
    -I "$WORK/upstream" -I "$REPO_ROOT/vendor" \
    "$REPO_ROOT/tests/tools/dump_seq_upstream.cpp" "$WORK/upstream/generator.cpp" \
    -lcurl -o "$WORK/dump_upstream"

# 2) Refactor dumper — built from THIS tree's core.
"$CXX" $STD \
    -I "$REPO_ROOT/include" -I "$REPO_ROOT/vendor" \
    "$REPO_ROOT/tests/tools/dump_seq_refactor.cpp" \
    "$REPO_ROOT/src/generator.cpp" "$REPO_ROOT/src/protocol.cpp" \
    -lcurl -o "$WORK/dump_refactor"

"$WORK/dump_upstream" "$DB" | sort > "$WORK/upstream.txt"
"$WORK/dump_refactor" "$DB" | sort > "$WORK/refactor.txt"

U_LINES=$(wc -l < "$WORK/upstream.txt")
R_LINES=$(wc -l < "$WORK/refactor.txt")
U_BYTES=$(tr -d '\n\t|' < "$WORK/upstream.txt" | wc -c)
echo "-----------------------------------------------------------------"
echo "models emitted (upstream): $U_LINES"
echo "models emitted (refactor): $R_LINES"
echo "hex nibbles compared     : $U_BYTES  (= $((U_BYTES / 2)) bytes)"

if diff -u "$WORK/upstream.txt" "$WORK/refactor.txt" > "$WORK/diff.txt"; then
    echo "-----------------------------------------------------------------"
    echo "RESULT: IDENTICAL — every emitted byte matches upstream over $R_LINES models."
    exit 0
else
    echo "-----------------------------------------------------------------"
    echo "RESULT: DIFFERENCES FOUND:"
    head -40 "$WORK/diff.txt"
    exit 1
fi
