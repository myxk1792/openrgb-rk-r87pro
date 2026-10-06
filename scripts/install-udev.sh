#!/bin/sh
# Install the udev rule that lets OpenRGB (and r87proctl) open the vendor HID
# interface of the Royal Kludge R87 Pro.
#
#   sudo ./scripts/install-udev.sh
#
# OpenRGB ships 60-openrgb.rules but it does not list PID 019F, so the hidraw
# node stays root-only (0600) and the keyboard cannot be opened by the plugin.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RULE=61-openrgb-rk-r87pro.rules

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root:  sudo $0" >&2
    exit 1
fi

install -m 0644 "$ROOT/udev/$RULE" "/etc/udev/rules.d/$RULE"
udevadm control --reload
udevadm trigger --subsystem-match=hidraw --action=change
udevadm trigger --subsystem-match=usb --action=change

echo "installed /etc/udev/rules.d/$RULE"
echo
echo "Check access with:"
echo "  ./tools/r87proctl info"
