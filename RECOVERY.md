# R87 Pro 刷机 / 抢救记录（2026-10-06）

本文件记录一次真实事故的完整恢复流程，供以后复用。

## 事故经过

为了找「关掉固件自带灯效层」的命令，我向键盘发送了一批**厂商探测命令**
（report 0x05 的区域命令、以及 reg=0x00–0x0C 的 520 字节「模式包」扫描）。
之后键盘出现：**USB 设备正常、厂商通道能应答、但完全不输出按键**，
而且拔插（键盘带电池，MCU 未必真正断电）也无法恢复。最终通过**重刷官方固件**解决。

> 结论：不要对这块键盘盲扫未知的厂商命令。LED 颜色协议（report 0x06 + reg 0x08）是安全的，
> 其它 reg / report 0x05 命令可能写坏固件里的配置区。

## 一、进入 ISP 刷机模式

正常固件运行时，向 `/dev/hidrawX`（接口 1、usage page 0xFF00）发送：

```
feature report 0x05 = 05 75 00 00 00 00
```

用本仓库的工具：

```sh
./tools/r87proctl cmd 05 75 00 00 00 00
```

设备随后重新枚举为 **`0603:1020` "Gaming KB"（SINO WEALTH ISP bootloader）**。
该引导设备的接口是 0 端点的 HID，**不会生成 hidraw 节点**，必须走 USB（libusb / sinowisp）。

## 二、USB 访问权限

安装 udev/62-sinowisp.rules：

```sh
sudo install -m 0644 udev/62-sinowisp.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
```

## 三、用 sinowisp 读写 flash

```sh
cargo install sinowisp

# 备份（61440 字节）
sinowisp read  --format bin -p sh68f90 --vendor_id 0x0603 --product_id 0x1020 \
    --firmware_size 61440 --bootloader_size 4096 backup.bin

# 写入（写完会自动 enable firmware + reboot）
sinowisp write -p sh68f90 --vendor_id 0x0603 --product_id 0x1020 \
    --firmware_size 61440 --bootloader_size 4096 firmware.bin
```

该型号 bootloader 的 ISP MD5 为 `964958a269b77ac084b6c4ccb0265360`。

## 四、退出 ISP 模式

**关键**：必须先 `enable firmware` 再 `reboot`；只发 reboot 会重新回到引导程序。

```sh
./tools/isp_exit
```

编译：`cc -O2 -o isp_exit isp_exit.c $(pkg-config --cflags --libs libusb-1.0)`（工具源码就是 [tools/isp_exit.c](tools/isp_exit.c)）

它发送的两条报文：

```
feature report 0x05 = 05 55 00 00 00 00   (enable firmware)
feature report 0x05 = 05 5A 00 00 00 00   (reboot -> 设备重新枚举为 258a:019f)
```

## 五、固件镜像（仓库不附带二进制）

厂商固件属于 RK 的版权内容，且 dump 里包含本机状态，因此本仓库**不包含**任何固件 `.bin`。

自己备份 / 恢复：

```sh
# 任何一次改动前先备份
sinowisp read  --format bin -p sh68f90 --vendor_id 0x0603 --product_id 0x1020 \
    --firmware_size 61440 --bootloader_size 4096 my-backup.bin

# 需要时写回（会自动 enable firmware + reboot）
sinowisp write -p sh68f90 --vendor_id 0x0603 --product_id 0x1020 \
    --firmware_size 61440 --bootloader_size 4096 my-backup.bin
```

官方固件获取：`https://drive.rkgaming.com/down/work/RKWEB/firmware/R87PRO/firmware.json`
（version 1207 → `R87pro-V1207.exe`；其负载是加密的，必须由官方升级程序在 Windows 上解密写入）。

## 六、官方 Windows 工具的用处

`R87pro-V1207.exe` 在真 Windows（或 USB 直通的 Windows 虚拟机）里运行即可完成
「解密 + 进 ISP + 刷写 + 重启」全过程；本次就是靠它在虚拟机里写入了新固件。
注意虚拟机需要**同时**直通 `258A:019F` 和 `0603:1020`（升级过程中设备会换 PID）。
`RK_Keyboard_Software_Setup_V4.6.exe` 是配置软件，可写回 `Dev/019F/KB.ini` 里的键位/灯效配置。
