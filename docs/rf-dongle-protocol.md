# R87 Pro 2.4G dongle protocol (reverse engineered, not implemented yet)

Notes taken from the vendor web app (`https://drive.rkgaming.com/assets/index-*.js`
plus its workers `communication-*.js` and `dongleCommunication-*.js`).

## Device

The R87 Pro talks to its 2.4G receiver, which enumerates as:

```
3554:FA09  "CX 2.4G Wireless Receiver"
  interface 0 : boot keyboard (8 B) + vendor input, usage page 0xFF00 / usage 0x03
  interface 1 : mouse, consumer, system, NKRO keyboard
                usage page 0xFF02 / usage 0x02, Report ID 0x13 = 19 B input + 19 B output
                usage page 0xFF04 / usage 0x02, Report ID 0x06 = 8 B feature
```

The vendor app describes the keyboard in dongle mode as:

```js
{ name: 'RK-R87PRO', vendorId: 13652 /*0x3554*/, productId: 64009 /*0xFA09*/,
  usagePage: 65282 /*0xFF02*/, usage: 2, connectType: 'Dongle', protocol: oKe }
```

so all control traffic goes through **output/input report 0x13 (19 bytes)** on interface 1.
The 0xFF00/0xFF04 collections are not used by this protocol.

## Transport

A worker keeps a queue of 19-byte reports and paces them:

* one report is sent, then the same report is **repeated every ~15 ms until the device answers**,
* a 3000 ms timeout aborts the current packet,
* a keepalive report is sent every 1000 ms when the queue is empty.

Responses arrive as 19-byte **input** reports; byte 0 is the command id.

## 19-byte packet layout (request)

```
byte  0      : command id
byte  1      : 127 & packageNum          (number of packets in the transfer)
byte  2      : 127 & packageIndex        (0-based index of this packet)
byte  3      : (15 & dataLength) | ((board & 0x0F) << 4)
byte  4..17  : up to 14 payload bytes     (AK = 14)
byte 18      : CRC = (19 + sum(byte 0..16)) & 0xFF
```

`packageNum = ceil(len / 14)`; a packet is acknowledged by an input report whose byte 1 has
bit 7 clear (`byte1 >> 7 == 0`), otherwise the packet is retried (`KR = 10` attempts).

Responses of the `get*` commands use: byte 4 = packageNum, byte 5 = packageIndex,
byte 6..7 = dataLength (little endian), payload follows.

## Command ids

```
1  SetKeyMatrix      65 GetKeyMatrix
2  SetLedColors      66 GetLedColors
3  SetMacros         67 GetMacros
4  SetProfile        68 GetProfile
5  GetPassword       73 GetLedEffect
6  SetFactory        10 ActivelyReport (dongle -> host)
7  GetDongleStatus
9  SetLedEffect
```

LED colour data is sent with `SetLedColors` (id 2) as a flat buffer of RGB triplets
(the wired protocol uses 3 bytes per LED as well, 126 LEDs -> 378 bytes).

## Bandwidth

378 bytes / 14 bytes per packet = 27 packets; at one acknowledged packet per ~15 ms a frame
takes roughly 0.4 s, i.e. a few frames per second. Fine for static colours, not for fast effects.

## Status

Not implemented in this plugin: the wired path (519-byte feature report frames) still needs the
USB cable. Implementing the dongle path would mean a second transport in `src/` that speaks the
protocol above over hidraw output/input report 0x13.
