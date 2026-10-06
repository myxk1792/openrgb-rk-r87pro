#!/bin/sh
# Remove the plugin from the OpenRGB user plugin directory.
set -e

TARGET="${XDG_CONFIG_HOME:-$HOME/.config}/OpenRGB/plugins/rk-r87pro.so"

rm -f "$TARGET"
echo "removed $TARGET (re-run with sudo if the file was installed elsewhere)"

if [ "$(id -u)" -eq 0 ]; then
    rm -f /etc/udev/rules.d/61-openrgb-rk-r87pro.rules
    udevadm control --reload 2>/dev/null || true
    echo "removed /etc/udev/rules.d/61-openrgb-rk-r87pro.rules"
fi
