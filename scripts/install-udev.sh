#!/bin/sh
# Install the udev rule that lets OpenRGB (and r87proctl) open the vendor HID
# interface of the Royal Kludge R87 Pro.
#
#   sudo ./scripts/install-udev.sh
#
# OpenRGB ships 60-openrgb.rules but it lists neither the keyboard (258A:019F)
# nor its 2.4G receiver (3554:FA09), so their hidraw nodes stay root-only (0600)
# and the plugin cannot open them.  All rules in udev/ are installed.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root:  sudo $0" >&2
    exit 1
fi

for rule in "$ROOT"/udev/*.rules; do
    name=$(basename "$rule")
    install -m 0644 "$rule" "/etc/udev/rules.d/$name"
    echo "installed /etc/udev/rules.d/$name"
done

udevadm control --reload
udevadm trigger --subsystem-match=hidraw --action=change
udevadm trigger --subsystem-match=usb --action=change
echo
echo "Check access with:"
echo "  ./tools/r87proctl info"
