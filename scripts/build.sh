#!/bin/sh
# Build the OpenRGB plugin and the standalone diagnostic tool.
#
#   ./scripts/build.sh
#
# Requirements: cmake, a C++17 compiler, Qt6 (Core/Gui/Widgets) development
# files and hidapi (pkg-config module hidapi-hidraw).
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)

echo "== configuring =="
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release

echo "== building plugin =="
cmake --build "$ROOT/build" -j"$(nproc 2>/dev/null || echo 4)"

echo "== building r87proctl =="
mkdir -p "$ROOT/tools"
cc -O2 -Wall -Wextra -o "$ROOT/tools/r87proctl" "$ROOT/tools/r87proctl.c" \
   $(pkg-config --cflags --libs hidapi-hidraw)

echo
echo "plugin : $ROOT/build/plugins/rk-r87pro.so"
echo "tool   : $ROOT/tools/r87proctl"
