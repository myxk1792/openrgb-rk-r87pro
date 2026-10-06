# OpenRGB plugin: Royal Kludge R87 Pro RGB control

Adds RGB control to the **Royal Kludge R87 Pro** keyboard (USB `258A:019F`,
BY Tech / SinoWealth chip) in the form of an OpenRGB plugin.

OpenRGB 1.0 ships `SinowealthControllerDetect`, which covers several PIDs under vendor
258A (0x0016, 0x0090, 0x010C, ...) but **not 0x019F**, so this keyboard cannot be detected
by OpenRGB at all. This plugin fills that gap.

Verified on real hardware: the plugin registers, Direct mode lights individual keys, and all
87 key LED indices were checked key by key (see section 8).

[![Build and release plugin](https://github.com/myxk1792/openrgb-rk-r87pro/actions/workflows/release.yml/badge.svg)](https://github.com/myxk1792/openrgb-rk-r87pro/actions/workflows/release.yml)

*中文说明见 [README_zh.md](README_zh.md).*

---

## 1. Target device

| Item | Value |
| --- | --- |
| USB VID:PID | `258A:019F` |
| Manufacturer / product string | `BY Tech` / `R87PRO` |
| Firmware model ID | **0x56** (queried via feature report 0x06) |
| Keyboard interface | interface 0 = standard HID keyboard (hidrawN) |
| RGB interface | interface 1 = vendor collection (hidrawN) |
| RGB collection | Usage Page `0xFF00`, Usage `0x01` |
| Data channel | Feature Report **0x06**, 519 data bytes (520 through hidapi) |
| LED count | 102 (indices 0..101; the populated indices are listed in section 8) |

## 2. Protocol

The report descriptor of the vendor collection defines Report ID 3 (Input, 3 bytes),
Report ID 5 (Feature, 5 bytes) and **Report ID 6 (Feature, 519 bytes, RGB data)**.

This is the **same vendor protocol** that OpenRGB already speaks through
`SinowealthKeyboard10cController` (PID 0x010C, used by AULA / LEOBOG / Redragon boards
with the same chip), so the plugin reuses its packet layout:

**Query the model ID** (measured response: `06 82 01 00 01 00 06 00 03 00 00 00 01 56 ...`)

```
send feature report 0x06: 06 82 01 00 01 00 06 00 ...
read feature report 0x06: response byte [13] = model ID (0x56)
```

**Direct mode colour frame (520 bytes per frame)**

```
offset 0x00 : 0x06          feature report ID
       0x01 : 0x08          direct mode
       0x04 : 0x01
       0x06 : 0x7A
       0x07 : 0x01
       0x08 : R G B  R G B  ...   RGB triplets starting at LED 0
```

The firmware falls back to its built-in hardware effects when frames stop arriving, so the
plugin (and `r87proctl`) resend the last frame roughly once per second to stay in direct mode,
matching the keepalive of the official OpenRGB 010C driver.

The official Windows software (**RK Keyboard Software V4.6**, whose `app/Dev/019F/KB.ini`
explicitly lists this model) uses the packet format
`<reportID> <reg> b2 b3 b4 b5 <len16 little-endian> <payload@0x08>`; the `7A 01` in the colour
frame is simply the payload length 0x017A = 378 = 3 x 126, not a magic number.

LED indices follow the matrix wiring: `index = column * 6 + row`. The concrete mapping is in
section 8.

## 3. Layout

```
rk-r87pro-plugin/
├── CMakeLists.txt
├── include/                      # SDK headers taken from OpenRGB 1.0 (GPL-2.0-or-later)
│   ├── OpenRGBPluginInterface.h
│   ├── RGBControllerInterface.h
│   └── filesystem.h
├── src/
│   ├── R87ProPlugin.{h,cpp}      # plugin entry point (OpenRGBPluginInterface)
│   ├── R87ProDevice.{h,cpp}      # HID transport + direct-mode keepalive
│   ├── R87ProLayout.{h,cpp}      # LED index <-> key mapping (measured)
│   └── metadata.json             # Qt plugin metadata (API version 5)
├── tools/
│   ├── r87proctl.c               # standalone diagnostic / calibration tool
│   └── isp_exit.c                # leave the ISP bootloader (libusb)
├── udev/
│   ├── 61-openrgb-rk-r87pro.rules
│   └── 62-sinowisp.rules
├── scripts/
│   ├── build.sh
│   ├── install-plugin.sh
│   ├── install-udev.sh
│   ├── install-system.sh
│   ├── uninstall-plugin.sh
│   └── verify-keys.sh            # light anchor keys one by one for manual checking
├── README.md                     # this file
├── README_zh.md                  # Chinese version
└── RECOVERY.md                   # flashing / rescue notes (Chinese)
```

The plugin does **not** link against any internal OpenRGB symbol: it only uses the
`OpenRGBPluginAPIInterface` handed to it by `Load()`, and registers a controller with callbacks
through `CreateVirtualRGBController()`. It therefore also works with distribution-packaged
OpenRGB builds, which do not export internal symbols.

## 4. Dependencies

* cmake >= 3.16 and a C++17 compiler
* Qt6 Core / Gui / Widgets (development packages)
* hidapi (`pkg-config hidapi-hidraw`; `hidapi` on Arch)
* nlohmann-json (Arch: `nlohmann-json`; Debian/Ubuntu: `nlohmann-json3-dev`) - referenced by the OpenRGB SDK headers

## 5. Building

```sh
./scripts/build.sh
```

Outputs:

* `build/plugins/rk-r87pro.so` - the OpenRGB plugin
* `tools/r87proctl` - the diagnostic tool

## 6. Installation

### 6.1 Prebuilt packages (quickest)

Download `rk-r87pro-linux-x86_64-*.tar.gz` for your distribution from
[Releases](https://github.com/myxk1792/openrgb-rk-r87pro/releases) (older glibc:
`ubuntu22.04`, newer: `ubuntu24.04`), then unpack and install:

```sh
mkdir -p ~/.config/OpenRGB/plugins
cp rk-r87pro.so ~/.config/OpenRGB/plugins/

sudo cp udev/61-openrgb-rk-r87pro.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw --action=change
```

The tarball also contains `r87proctl` (LED diagnostics), `isp_exit` (leave ISP flashing mode)
and the udev rules; `BUILD-INFO.txt` records the Qt / glibc versions used to build it. You can
of course build from source instead (section 5).

The packages are built by GitHub Actions on Ubuntu 22.04 / 24.04 images (Qt 6.2.4 / 6.4,
glibc >= 2.35). One of them was loaded and registered successfully in an OpenRGB built against
Qt 6.11; Qt keeps binary compatibility within 6.x, so either package normally works - pick the
one whose glibc is closer to your system.

### 6.2 Plugin (per user, no root needed)

```sh
./scripts/install-plugin.sh
```

This copies `rk-r87pro.so` into `$XDG_CONFIG_HOME/OpenRGB/plugins/` (default
`~/.config/OpenRGB/plugins/`), which is exactly the OpenRGB user plugin directory.

### 6.3 udev rules (required, needs root)

`258A:019F` is **not** listed in the `60-openrgb.rules` shipped with OpenRGB, so `/dev/hidrawN`
is `crw------- root root` and neither OpenRGB nor the plugin can open it. Install the rule that
ships with this plugin:

```sh
sudo ./scripts/install-udev.sh
```

The rule itself (`udev/61-openrgb-rk-r87pro.rules`):

```
SUBSYSTEMS=="usb|hidraw", ATTRS{idVendor}=="258a", ATTRS{idProduct}=="019f", TAG+="uaccess"
```

Afterwards replug the keyboard (or run `sudo udevadm trigger`) and check:

```sh
./tools/r87proctl info
```

### 6.4 When the system ships an openrgb.service (important)

Some distributions (for example the Arch `openrgb` package) install
`/usr/lib/systemd/system/openrgb.service`, which runs `openrgb --server --config /etc/openrgb`
as root, holds the keyboard device and listens on port 6742. The OpenRGB GUI then
**automatically connects to that already running server as a client**, and consequently does
**not** load plugins from `~/.config/OpenRGB/plugins`.

In that situation, install the plugin into the server configuration directory as well and let
the server expose plugin controllers to clients:

```sh
sudo ./scripts/install-system.sh
```

That script installs the plugin into `/etc/openrgb/plugins/`, sets `Server.all_controllers` to
`true` in `/etc/openrgb/OpenRGB.json` (after backing it up to `.bak`) and restarts
`openrgb.service`.

Alternatively drop the service and let the GUI load the plugin from the user directory:

```sh
sudo systemctl disable --now openrgb.service
```

Both ways the plugin is in place (user directory plus system directory); pick either.

## 7. Usage

1. Start OpenRGB. The plugin adds an **Information** tab page named "RK R87 Pro" showing the
   model ID, LED count and device path, plus a *Rescan* button.
2. A **Royal Kludge R87 Pro** device appears on the **Devices** tab with the modes `Off` and
   `Direct`. Select `Direct` to set per-key colours (other effect plugins can drive it too).

The startup log (`~/.config/OpenRGB/logs/`) shows:

```
[RK R87 Pro] plugin loaded (plugin API 5)
[RK R87 Pro] device at /dev/hidrawN reports model ID 0x56 (Royal Kludge R87 Pro)
[RK R87 Pro] OpenRGB reports 1 controllers after registration
[PluginManager] Registering RGB controller Royal Kludge R87 Pro
```

### Verifying through the SDK (optional)

Plugin controllers are a distinct category, and the OpenRGB SDK server only exposes hardware
controllers to clients by default. To let a GUI connected as a client see it, enable
**Server -> Serve All Controllers** (`Server.all_controllers = true` in `OpenRGB.json`).
Measured with that enabled:

```sh
$ openrgb --list-devices
0: Royal Kludge R87 Pro
$ openrgb --list-detailed
0: Royal Kludge R87 Pro
  Type:        Keyboard
  Modes:       Off [Direct]
  Zones:       Keyboard
  LEDs:        'Key: Escape' 'Key: `' 'Key: Tab' ... 'Key: Right Arrow'
$ openrgb -d 0 -m Direct -c 00FF00
```

### Diagnostic / calibration tool

```sh
./tools/r87proctl info                        # enumerate devices, read the model ID, dump feature 0x06
./tools/r87proctl fill ff0000                 # everything red, Ctrl+C to stop
./tools/r87proctl test 0                      # light LED 0 only (red for 5 s by default)
./tools/r87proctl test 89 00ffff 6            # light LED 89 only, cyan, 6 s
./tools/r87proctl sweep 0 112                 # light LEDs one by one to map the board
./tools/r87proctl multi 0:ff0000 94:00ff00 20 # light several LEDs at once
./tools/r87proctl off                         # all off
./scripts/verify-keys.sh                      # walk through the anchor keys
```

## 8. Measured LED mapping (model ID 0x56)

The table below is the result of lighting LEDs one by one and checking every key by hand
(`index = column * 6 + row`); it also matches the last column of the `[KEY]` section of the
official vendor configuration `RK_Keyboard_Software/Dev/019F/KB.ini`, entry by entry (the vendor
file additionally lists `Mute=100`, for which no LED exists on this board):

```
row0  Esc=0    F1=12  F2=18  F3=24  F4=30  F5=36  F6=42  F7=48  F8=54  F9=60  F10=66  F11=72  F12=78   PrtSc=84  ScrLk=90  Pause=96
row1  `=1      1=7    2=13   3=19   4=25   5=31   6=37   7=43   8=49   9=55   0=61   -=67   ==73   BSpc=79   Ins=85    Home=91   PgUp=97
row2  Tab=2    Q=8    W=14   E=20   R=26   T=32   Y=38   U=44   I=50   O=56   P=62   [=68   ]=74   \=80     Del=86    End=92    PgDn=98
row3  Caps=3   A=9    S=15   D=21   F=27   G=33   H=39   J=45   K=51   L=57   ;=63   '=69          Enter=81
row4  LShift=4 Z=10   X=16   C=22   V=28   B=34   N=40   M=46   ,=52   .=58   /=64                  RShift=82  Up=94
row5  LCtrl=5  LWin=11 LAlt=17 Space=35 RAlt=53 Fn=59 Menu=65                                       RCtrl=83   Left=89   Down=95   Right=101
```

Empty indices without an LED: 6, 23, 29, 41, 47, 70, 71, 75, 76, 77, 87, 88, 93, 100, 107.

### Calibrating another batch

Other batches or firmware revisions may differ. If some key lights up in the wrong place you do
**not** need to touch the code - drop an override file at
`~/.config/OpenRGB/rk-r87pro-layout.json`:

```json
{
  "keys": {
    "Key: Right Control": 83,
    "Key: Left Arrow": 89
  }
}
```

Key names are the standard OpenRGB names (`Key: Escape`, `Key: A`, `Key: Up Arrow`, ...; the full
list is in `src/R87ProLayout.cpp`) and the values are hardware LED indices (find them with
`r87proctl sweep` / `multi`). Keys that are not listed keep their defaults; the plugin logs the
applied overrides when it starts.

## 9. Uninstalling

```sh
./scripts/uninstall-plugin.sh          # remove the plugin
sudo ./scripts/uninstall-plugin.sh     # also remove the udev rule
```

## 10. Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| No keyboard in the device list | Check `~/.config/OpenRGB/logs/`: a "could not open /dev/hidrawN" message means the udev rule is missing. Install it, replug the keyboard and hit *Rescan* on the plugin page. |
| The plugin page says "No keyboard registered" | Same as above; you may also be using the wireless receiver (`3554:FA09` is not an RGB keyboard). |
| Colours flash once and the old effect returns | The keepalive thread is stalled; make sure the OpenRGB process is still running. |
| Some keys show the wrong colour | Calibrate with the override file from section 8. |
| The log shows a model ID other than 0x56 | Different batch; re-check the LED indices with `r87proctl sweep` and fix them through the override file. |

## 11. Implementation notes and known limitations

* Only `Off` and `Direct` modes are implemented; the built-in hardware effects (breathing,
  wave, ...) are not exposed.
* **Known unsolved issue: the white flashing layer produced by the firmware itself is still
  there.** With every LED set to a non-zero colour (say blue), the keyboard keeps cycling
  blue, blue, white - the firmware animation layer stays on top. All of the following were
  tried and **none worked**: changing frame header byte[1] (0x00/0x01/0x0F, ...), writing the
  mode bytes at 0x14=0x01 / 0x15=0x0F the way the related OpenRGB driver does, sending
  report 0x05 `{05 83 ...}` / `{05 01 AA BB 2F 3E}` before the colour frame, and sweeping reg
  (byte[1]) from 0x00 to 0x0C before streaming colour frames; the Fn+1 ... Fn+5 combos on the
  keyboard do nothing either. The official software splits "effect/mode settings" and
  "per-key colours" into two packets (the effect packet carries an effect index, effect colour
  and brightness/speed fields), but **the exact report ID / reg / offsets of that effect packet
  have not been worked out** for this 519-byte protocol, so the plugin cannot switch the
  animation layer off.
* Per-key colour itself is correct (checked key by key), so this only affects the keyboard
  animating by itself; it does not affect the LED index mapping.
* `Unload()` only unregisters the controller from OpenRGB and **deliberately does not delete the
  controller object**: in OpenRGB 1.0, `OpenRGBDialog::closeEvent` unloads the plugin `.so`
  (dlclose) *before* destroying the device page; deleting the controller at that point makes the
  device page destructor dereference a dangling pointer and OpenRGB segfaults on exit (reproduced
  and worked around). The controller object is a few KB and the process is about to exit, so it
  is intentionally left alone.
* This protocol is the result of community reverse engineering (based on the OpenRGB Sinowealth
  010C driver) and may change with firmware revisions.

## 12. Flashing / rescue

See [RECOVERY.md](RECOVERY.md) (Chinese): how to enter and leave the ISP flashing mode, and how
to back up and write the flash with sinowisp. (The repository does not ship vendor firmware
binaries; see section 5 of that document for where to get them.)

## 13. Licence

GPL-2.0-or-later (same as OpenRGB and its plugin SDK).
