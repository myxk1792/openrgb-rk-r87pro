#!/bin/sh
# Install the built plugin into the OpenRGB user plugin directory.
#
#   ./scripts/install-plugin.sh
#
# The directory follows XDG_CONFIG_HOME (defaults to ~/.config), which is the
# same location OpenRGB scans for user plugins.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLUGIN="$ROOT/build/plugins/rk-r87pro.so"
TARGET="${XDG_CONFIG_HOME:-$HOME/.config}/OpenRGB/plugins"

if [ ! -f "$PLUGIN" ]; then
    echo "plugin not built yet, run ./scripts/build.sh first" >&2
    exit 1
fi

mkdir -p "$TARGET"
install -m 0644 "$PLUGIN" "$TARGET/rk-r87pro.so"

echo "installed $TARGET/rk-r87pro.so"
echo
echo "Next step (once per machine, needs root):"
echo "  sudo $ROOT/scripts/install-udev.sh"
