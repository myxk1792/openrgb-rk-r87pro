# OpenRGB 插件：Royal Kludge R87 Pro RGB 控制

给 **Royal Kludge R87 Pro**（USB `258A:019F`，BY Tech / SinoWealth 芯片）键盘
添加 RGB 控制能力的 OpenRGB 插件。

OpenRGB 1.0 自带的 `SinowealthControllerDetect` 支持 258A 下的多个 PID
（0x0016、0x0090、0x010C 等），但**不包含 0x019F**，所以这把键盘在 OpenRGB 里
完全检测不到。本插件补上这个缺口。

本插件已在实机 R87 Pro 上**实测通过**：插件注册成功、Direct 模式可以逐键点亮、
87 个按键的 LED 索引全部逐个核对过（见第 8 节）。

[![Build and release plugin](https://github.com/myxk1792/openrgb-rk-r87pro/actions/workflows/release.yml/badge.svg)](https://github.com/myxk1792/openrgb-rk-r87pro/actions/workflows/release.yml)

---

## 1. 目标设备

| 项目 | 值 |
| --- | --- |
| USB VID:PID | `258A:019F` |
| 厂商 / 产品字符串 | `BY Tech` / `R87PRO` |
| 固件型号 ID | **0x56**（由 feature report 0x06 查询得到） |
| 键盘接口 | interface 0 = 标准 HID 键盘（hidrawN） |
| RGB 接口 | interface 1 = 厂商集合（hidrawN） |
| RGB 集合 | Usage Page `0xFF00`, Usage `0x01` |
| 数据通道 | Feature Report **0x06**，519 字节数据（hidapi 中为 520 字节） |
| LED 数量 | 102（索引 0..101，实测有效索引见第 8 节） |

## 2. 协议

厂商集合的 report descriptor 里定义了 Report ID 3（Input，3 字节）、Report ID 5
（Feature，5 字节）和 **Report ID 6（Feature，519 字节，RGB 数据）**。

这与 OpenRGB 中 `SinowealthKeyboard10cController`（PID 0x010C，AULA / LEOBOG /
Redragon 等同芯片键盘）使用的是**同一套厂商协议**，因此插件复用了它的报文格式：

**查询型号 ID**（实测响应：`06 82 01 00 01 00 06 00 03 00 00 00 01 56 ...`）

```
发送 feature report 0x06: 06 82 01 00 01 00 06 00 ...
读取 feature report 0x06: 响应字节 [13] = 型号 ID (0x56)
```

**Direct 模式写颜色（每帧 520 字节）**

```
偏移  0x00 : 0x06          特性报告 ID
      0x01 : 0x08          direct 模式
      0x04 : 0x01
      0x06 : 0x7A
      0x07 : 0x01
      0x08 : R G B  R G B  ...   从 LED 0 开始的 RGB 三元组
```

固件在停止收到帧之后会退回硬件灯效，所以插件（以及 `r87proctl`）会以约 1 秒的间隔
重复发送最后一帧以保持 direct 模式 —— 与 OpenRGB 官方 010C 驱动的 keepalive 一致。

官方 Windows 软件（**RK Keyboard Software V4.6**，其中 `app/Dev/019F/KB.ini` 明确支持本型号）
的报文格式为 `<reportID> <reg> b2 b3 b4 b5 <len16 小端> <payload@0x08>`：颜色帧里的
`7A 01` 就是负载长度 0x017A = 378 = 3 × 126，并不是魔法数。

LED 索引遵循矩阵布线：`index = 列 × 6 + 行`，具体对应关系见第 8 节。

## 3. 目录结构

```
rk-r87pro-plugin/
├── CMakeLists.txt
├── include/                      # 取自 OpenRGB 1.0 源码的 SDK 头文件（GPL-2.0-or-later）
│   ├── OpenRGBPluginInterface.h
│   ├── RGBControllerInterface.h
│   └── filesystem.h
├── src/
│   ├── R87ProPlugin.{h,cpp}      # 插件入口（OpenRGBPluginInterface）
│   ├── R87ProDevice.{h,cpp}      # HID 驱动 + direct 模式 keepalive
│   ├── R87ProLayout.{h,cpp}      # LED 索引 <-> 按键映射（已实测）
│   └── metadata.json             # Qt 插件元数据（API 版本 5）
├── tools/
│   ├── r87proctl.c               # 独立诊断/校准工具（不依赖 OpenRGB）
│   └── r87proctl
├── udev/
│   └── 61-openrgb-rk-r87pro.rules
├── scripts/
│   ├── build.sh
│   ├── install-plugin.sh
│   ├── install-udev.sh
│   ├── uninstall-plugin.sh
│   ├── verify-keys.sh            # 逐个点亮锚点键做人工核对
│   └── live-test.sh              # 在真实键盘上跑一次端到端测试
└── README.md
```

插件**不链接** OpenRGB 的任何内部符号：它只使用 `Load()` 拿到的
`OpenRGBPluginAPIInterface`，通过 `CreateVirtualRGBController()` 注册一个带回调的
控制器。因此对发行版打包的 OpenRGB（未导出内部符号）同样有效。

## 4. 依赖

* cmake ≥ 3.16、C++17 编译器
* Qt6 Core / Gui / Widgets（开发包）
* hidapi（`pkg-config hidapi-hidraw`；Arch 上是 `hidapi`）
* nlohmann-json（Arch: `nlohmann-json`；Debian/Ubuntu: `nlohmann-json3-dev`）——OpenRGB SDK 头文件会引用它

## 5. 构建

```sh
./scripts/build.sh
```

产物：

* `build/plugins/rk-r87pro.so` —— OpenRGB 插件
* `tools/r87proctl` —— 诊断工具

## 6. 安装

### 6.1 用预编译包（最快）

从 [Releases](https://github.com/myxk1792/openrgb-rk-r87pro/releases) 下载与发行版匹配的
`rk-r87pro-linux-x86_64-*.tar.gz`（glibc 较旧选 `ubuntu22.04`，较新选 `ubuntu24.04`），解压后：

```sh
mkdir -p ~/.config/OpenRGB/plugins
cp rk-r87pro.so ~/.config/OpenRGB/plugins/

sudo cp udev/61-openrgb-rk-r87pro.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw --action=change
```

包内还附带 `r87proctl`（灯位诊断）、`isp_exit`（退出 ISP 刷机模式）和 udev 规则；
`BUILD-INFO.txt` 记录了构建时的 Qt / glibc 版本。也可以自己编译（见第 5 节）。

### 6.2 插件（普通用户，无需 root）

```sh
./scripts/install-plugin.sh
```

会把 `rk-r87pro.so` 复制到 `$XDG_CONFIG_HOME/OpenRGB/plugins/`
（默认 `~/.config/OpenRGB/plugins/`），这正是 OpenRGB 的用户插件目录。

### 6.3 udev 规则（必须，需要 root）

`258A:019F` **不在** OpenRGB 自带的 `60-openrgb.rules` 中，因此 `/dev/hidrawN` 是
`crw------- root root`，OpenRGB 和插件都打不开。安装本插件附带的规则：

```sh
sudo ./scripts/install-udev.sh
```

规则内容（`udev/61-openrgb-rk-r87pro.rules`）：

```
SUBSYSTEMS=="usb|hidraw", ATTRS{idVendor}=="258a", ATTRS{idProduct}=="019f", TAG+="uaccess"
```

装好后重新插拔键盘（或 `sudo udevadm trigger`），确认：

```sh
./tools/r87proctl info
```

### 6.4 系统装有 openrgb.service 时（重要）

某些发行版（例如 Arch 的 openrgb 包）会安装 `/usr/lib/systemd/system/openrgb.service`，它以 root 运行
`openrgb --server --config /etc/openrgb`，占用键盘设备和 6742 端口；OpenRGB GUI 启动时会
**自动连接**到这个已在运行的服务器当客户端，因此**不会**加载 `~/.config/OpenRGB/plugins`
里的插件。

这种情况下把插件同时装到服务的配置目录，并让服务器把插件控制器暴露给客户端：

```sh
sudo ./scripts/install-system.sh
```

该脚本会：安装插件到 `/etc/openrgb/plugins/`、把 `/etc/openrgb/OpenRGB.json` 的
`Server.all_controllers` 置为 `true`（先备份为 `.bak`）、重启 `openrgb.service`。

或者改为不用该服务，让 GUI 自己加载用户目录里的插件：

```sh
sudo systemctl disable --now openrgb.service
```

两种方式插件都已就位（用户目录 + 系统目录），任选其一即可。

## 7. 使用

1. 启动 OpenRGB，插件在 **Information** 标签页有一页 “RK R87 Pro”，显示型号 ID、
   LED 数量和设备路径，并提供 *Rescan* 按钮。
2. **Devices** 标签页出现 **Royal Kludge R87 Pro**，模式为 `Off` / `Direct`；
   选 `Direct` 即可逐键设置颜色（也能配合各类效果插件）。

启动日志里可以看到（`~/.config/OpenRGB/logs/`）：

```
[RK R87 Pro] plugin loaded (plugin API 5)
[RK R87 Pro] device at /dev/hidrawN reports model ID 0x56 (Royal Kludge R87 Pro)
[RK R87 Pro] OpenRGB reports 1 controllers after registration
[PluginManager] Registering RGB controller Royal Kludge R87 Pro
```

### 通过 SDK 验证（可选）

插件控制器属于“插件控制器”，OpenRGB 的 SDK 服务器默认只向客户端暴露硬件控制器。
若要让以客户端身份连接的 GUI 也能看到它，需启用 **Server → Serve All Controllers**
（即 `OpenRGB.json` 里的 `Server.all_controllers = true`）。启用后实测：

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

### 诊断 / 校准工具

```sh
./tools/r87proctl info                        # 枚举设备、读型号 ID、dump feature 0x06
./tools/r87proctl fill ff0000                 # 全部红色，Ctrl+C 退出
./tools/r87proctl test 0                      # 只点亮 LED 0（默认红色 5 秒）
./tools/r87proctl test 89 00ffff 6            # 只点亮 LED 89，青色，6 秒
./tools/r87proctl sweep 0 112                 # 逐个点亮，用来核对灯位
./tools/r87proctl multi 0:ff0000 94:00ff00 20 # 同时点亮若干 LED，便于一次核对多个
./tools/r87proctl off                         # 全部熄灭
./scripts/verify-keys.sh                      # 自动跑一遍锚点键核对
```

## 8. 实测灯位映射（型号 ID 0x56）

下表是逐个点亮、逐键人工核对后的结果（`index = 列×6 + 行`），并与**厂商官方配置**
`RK_Keyboard_Software/Dev/019F/KB.ini` 的 `[KEY]` 最后一列逐一核对一致（官方另有
`Mute=100`，该灯位实测无对应 LED）：

```
行0  Esc=0    F1=12  F2=18  F3=24  F4=30  F5=36  F6=42  F7=48  F8=54  F9=60  F10=66  F11=72  F12=78   PrtSc=84  ScrLk=90  Pause=96
行1  `=1      1=7    2=13   3=19   4=25   5=31   6=37   7=43   8=49   9=55   0=61   -=67   ==73   BSpc=79   Ins=85    Home=91   PgUp=97
行2  Tab=2    Q=8    W=14   E=20   R=26   T=32   Y=38   U=44   I=50   O=56   P=62   [=68   ]=74   \=80     Del=86    End=92    PgDn=98
行3  Caps=3   A=9    S=15   D=21   F=27   G=33   H=39   J=45   K=51   L=57   ;=63   '=69          Enter=81
行4  LShift=4 Z=10   X=16   C=22   V=28   B=34   N=40   M=46   ,=52   .=58   /=64                  RShift=82  Up=94
行5  LCtrl=5  LWin=11 LAlt=17 Space=35 RAlt=53 Fn=59 Menu=65                                       RCtrl=83   Left=89   Down=95   Right=101
```

无 LED 的空索引：6、23、29、41、47、70、71、75、76、77、87、88、93、100、107。

### 需要校准其他批次时

不同批次/固件可能不同。若发现某些键亮错位置，**不必改代码**，在 OpenRGB 配置目录放一个
覆盖文件 `~/.config/OpenRGB/rk-r87pro-layout.json`：

```json
{
  "keys": {
    "Key: Right Control": 83,
    "Key: Left Arrow": 89
  }
}
```

键名使用 OpenRGB 标准键名（`Key: Escape`、`Key: A`、`Key: Up Arrow` …，完整列表见
`src/R87ProLayout.cpp`），值是硬件 LED 索引（用 `r87proctl sweep` / `multi` 查找）。
未列出的键保持默认值；插件启动时会把覆盖情况写入 OpenRGB 日志。

## 9. 卸载

```sh
./scripts/uninstall-plugin.sh          # 删除插件
sudo ./scripts/uninstall-plugin.sh     # 同时删除 udev 规则
```

## 10. 故障排查

| 现象 | 原因 / 处理 |
| --- | --- |
| 设备列表里没有键盘 | 看 `~/.config/OpenRGB/logs/`：若出现 “could not open /dev/hidrawN”，说明 udev 规则没装；装好后重新插拔键盘，再点插件页的 *Rescan*。 |
| 插件页显示 “No keyboard registered” | 同上；也可能插的是无线接收器（`3554:FA09` 不是 RGB 键盘）。 |
| 颜色一闪就恢复原灯效 | keepalive 线程被挂起；确认 OpenRGB 进程仍在运行。 |
| 某些键颜色错位 | 用第 8 节的映射覆盖文件校准。 |
| 日志里型号 ID 不是 0x56 | 说明是另一批次；用 `r87proctl sweep` 重新核对灯位并用覆盖文件修正。 |

## 11. 实现说明与已知限制

* 只实现 `Off` 与 `Direct` 两种模式；键盘内置硬件灯效（呼吸、波浪等）未暴露。
* **已知未解决问题：固件自带的白色闪烁灯效层没有被关掉。** 表现为：给所有 LED 都设成
  非零颜色（例如全蓝）时，键盘仍是“蓝、蓝、白”循环闪烁——即固件的动画层一直在叠加。
  已尝试过但**均无效**的做法：改帧头 byte[1]（0x00/0x01/0x0F…）、按 OpenRGB 同族驱动把模式
  字节写到 0x14=0x01/0x15=0x0F、先发 report 0x05 的 `{05 83 …}` / `{05 01 AA BB 2F 3E}` 再写帧、
  以及把 reg（byte[1]）从 0x00 扫到 0x0C 后再持续发颜色帧；键盘自身的 Fn+1…Fn+5 组合键也无效。
  官方软件把“效果/模式设置”和“逐键颜色”分成两条报文（效果包内含效果索引、效果色、亮度/速度字段），
  但该 519 字节协议下**这条效果报文的确切 reportID/reg/偏移尚未解出**，因此插件目前无法自行关闭动画层。
* 逐键颜色本身是正确的（已逐个核对），因此该问题只影响“键盘自己还在闪”这一点，不影响灯位对应关系。
* `Unload()` 只把控制器从 OpenRGB 注销、**不删除控制器对象**：OpenRGB 1.0 在
  `OpenRGBDialog::closeEvent` 里先卸载插件 `.so`（dlclose），之后才销毁设备页；
  如果此时删除控制器，设备页析构会解引用悬空指针，导致 OpenRGB 退出时 SIGSEGV
  （已实测复现并规避）。控制器对象只有几 KB，且进程即将退出，因此选择不删除。
* 该协议为社区逆向结果（参考 OpenRGB 的 Sinowealth 010C 驱动），可能随固件版本变化。

## 12. 刷机 / 抢救

见 [RECOVERY.md](RECOVERY.md)：如何进入/退出 ISP 刷机模式、用 sinowisp 备份与写入 flash。
（仓库不含厂商固件二进制，获取方式见该文档第五节的说明。）

## 13. 许可

GPL-2.0-or-later（与 OpenRGB 及其插件 SDK 保持一致）。
