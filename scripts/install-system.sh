#!/bin/sh
# Install the plugin for the system-wide OpenRGB service.
#
#   sudo ./scripts/install-system.sh
#
# Why this exists: this machine runs /usr/lib/systemd/system/openrgb.service
# ("openrgb --server --config /etc/openrgb") as root.  That instance owns the
# hidraw devices and port 6742, and the OpenRGB GUI auto-connects to it as an
# SDK client - so a plugin installed only in ~/.config/OpenRGB/plugins would
# never be loaded.  This script installs the plugin into the service's own
# configuration directory and makes the server expose plugin controllers to
# its clients.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLUGIN="$ROOT/build/plugins/rk-r87pro.so"
SERVICE_DIR=/etc/openrgb

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root:  sudo $0" >&2
    exit 1
fi

if [ ! -f "$PLUGIN" ]; then
    echo "plugin not built yet - run ./scripts/build.sh first" >&2
    exit 1
fi

install -d -m 0755 "$SERVICE_DIR/plugins"
install -m 0644 "$PLUGIN" "$SERVICE_DIR/plugins/rk-r87pro.so"
echo "installed $SERVICE_DIR/plugins/rk-r87pro.so"

#---------------------------------------------------------------#
# The SDK server only serves controllers that come from hardware #
# unless "Serve All Controllers" is enabled; plugin controllers  #
# would otherwise be invisible to the GUI client.                 #
#---------------------------------------------------------------#
if [ -f "$SERVICE_DIR/OpenRGB.json" ]; then
    [ -f "$SERVICE_DIR/OpenRGB.json.bak" ] || cp "$SERVICE_DIR/OpenRGB.json" "$SERVICE_DIR/OpenRGB.json.bak"

    python3 - "$SERVICE_DIR/OpenRGB.json" <<'PY'
import json, sys
path = sys.argv[1]
with open(path) as handle:
    data = json.load(handle)
server = data.setdefault("Server", {})
if server.get("all_controllers") is True:
    print("Server.all_controllers already enabled")
else:
    server["all_controllers"] = True
    with open(path, "w") as handle:
        json.dump(data, handle, indent=4)
    print("enabled Server.all_controllers (backup: %s.bak)" % path)
PY
fi

systemctl restart openrgb.service
sleep 3
systemctl --no-pager --lines=3 status openrgb.service | head -8

echo
echo "Done.  Check the plugin log with:"
echo "  grep 'RK R87' $SERVICE_DIR/logs/*.log | tail -5"
