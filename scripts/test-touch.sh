#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_BIN="${TMPDIR:-/tmp}/touch_ios_test"

echo "=== Compiling PharaohPad Touch Input Tests ==="
g++ -std=c++17 \
    -I"$ROOT_DIR/tests/mock_sdl" \
    -I"$ROOT_DIR/src" \
    "$ROOT_DIR/src/input/touch.cpp" \
    "$ROOT_DIR/src/input/mouse.cpp" \
    "$ROOT_DIR/tests/touch_ios_test.cpp" \
    -o "$TEST_BIN"

echo "=== Executing Touch Input Tests ==="
"$TEST_BIN"
rm -f "$TEST_BIN"
