#!/bin/sh
# Optical verification: light one anchor LED at a time so a human can check
# that the Direct protocol works and that the LED index map matches the keys.
#
#   ./scripts/verify-keys.sh [seconds-per-led]
#
# Every entry below was measured on a real R87 Pro (model ID 0x56).
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOL="$ROOT/tools/r87proctl"
HOLD=${1:-4}

light() {
    printf '  LED %-3s -> %-14s (%s)\n' "$1" "$2" "$3"
    "$TOOL" test "$1" "$3" "$HOLD" || exit 1
    sleep 1
}

echo "Each LED below lights up alone for ${HOLD}s. Watch the keyboard."
echo
light 0   "Escape"      ff0000
light 12  "F1"          00ff00
light 35  "Space"       0000ff
light 81  "Enter"       ffff00
light 82  "Right Shift" 00ffff
light 83  "Right Ctrl"  ff00ff
light 89  "Left Arrow"  ffffff
light 94  "Up Arrow"    ff8000
light 95  "Down Arrow"  8000ff
light 101 "Right Arrow" 80ff00
light 98  "Page Down"   ff0080

"$TOOL" off || true
echo
echo "done - all LEDs off"
