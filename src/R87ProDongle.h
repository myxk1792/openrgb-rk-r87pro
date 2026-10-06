/*---------------------------------------------------------*\
| R87ProDongle.h                                            |
|                                                           |
|   2.4G receiver transport for the Royal Kludge R87 Pro.   |
|                                                           |
|   The CX receiver (3554:FA09) exposes the keyboard's      |
|   vendor channel as usage page 0xFF02 / usage 2 with a    |
|   19-byte report 0x13 (input + output).  Commands are     |
|   19-byte packets:                                        |
|                                                           |
|     byte  0     command id                                |
|     byte  1     127 & packageNum                          |
|     byte  2     127 & packageIndex                        |
|     byte  3     (15 & dataLength) | (board << 4)          |
|     byte  4..   up to 14 payload bytes                    |
|     byte 18     CRC = (19 + sum(byte 0..16)) & 0xFF       |
|                                                           |
|   Every packet is repeated until the device answers, and  |
|   the reply carries bit 7 of byte 1 clear when accepted.  |
|   Colours are a flat 126*3 buffer: 126 red bytes, then    |
|   126 green, then 126 blue.                               |
|                                                           |
|   Protocol reverse engineered from the vendor web app;    |
|   see docs/rf-dongle-protocol.md.                         |
|                                                           |
|   This file is part of the RK R87 Pro OpenRGB plugin      |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <hidapi.h>
#include "RGBControllerInterface.h"
#include "R87ProLayout.h"

namespace r87pro
{
    const unsigned int  DONGLE_VENDOR_ID  = 0x3554;
    const unsigned int  DONGLE_PRODUCT_ID = 0xFA09;
    const unsigned int  DONGLE_USAGE_PAGE = 0xFF02;
    const unsigned int  DONGLE_USAGE      = 0x0002;

    const unsigned char DONGLE_REPORT_ID  = 0x13;
    const unsigned int  DONGLE_PACKET_LEN = 19;
    const unsigned int  DONGLE_CHUNK      = 14;
    const unsigned int  DONGLE_LED_SLOTS  = 126;
    const unsigned int  DONGLE_COLOR_LEN  = DONGLE_LED_SLOTS * 3;

    const unsigned char DONGLE_CMD_SET_LED_COLORS  = 2;
    const unsigned char DONGLE_CMD_GET_PASSWORD    = 5;
    const unsigned char DONGLE_CMD_GET_DONGLE_STAT = 7;
    const unsigned char DONGLE_CMD_ACTIVELY_REPORT = 10;
    const unsigned char DONGLE_CMD_GET_LED_COLORS  = 66;

    const int           DONGLE_MODE_OFF    = 0;
    const int           DONGLE_MODE_DIRECT = 1;
}

class R87ProDongle
{
public:
    R87ProDongle(hid_device* dev_handle, const std::string& dev_path, const std::string& dev_name,
                 const r87pro::Layout& dev_layout);
    ~R87ProDongle();

    /*-----------------------------------------------------*\
    | Static callbacks handed to OpenRGB through            |
    | RGBController_Setup                                   |
    \*-----------------------------------------------------*/
    static void UpdateLEDsCallback(void* object);
    static void UpdateModeCallback(void* object);
    static void ConfigureZoneCallback(void* object, int zone);

    /*-----------------------------------------------------*\
    | Accessors                                             |
    \*-----------------------------------------------------*/
    const std::string&    GetName() const           { return(name); }
    const std::string&    GetLocation() const       { return(location); }
    const r87pro::Layout& GetLayout() const         { return(layout); }
    unsigned int          GetLEDCount() const       { return(layout.led_count); }
    unsigned char         GetModelID() const        { return(model_id); }
    const std::string&    GetFirmwareVersion() const { return(fw_version); }

    /*-----------------------------------------------------*\
    | Runtime                                               |
    \*-----------------------------------------------------*/
    bool Probe();
    void SetControllerInterface(RGBControllerInterface* iface);
    void Start();
    void Stop();
    void UpdateLEDs();

private:
    bool                  SendPacket(const unsigned char* pkt);
    int                   RecvPacket(unsigned char* pkt, int timeout_ms);
    void                  BuildPacket(unsigned char* pkt, unsigned char cmd, unsigned char board,
                                      unsigned char package_num, unsigned char package_index,
                                      unsigned char data_len) const;
    bool                  SimpleCommand(unsigned char cmd, unsigned char* reply, int timeout_ms);
    bool                  SendColors(const std::vector<RGBColor>& colors);
    std::vector<RGBColor> ReadControllerColors() const;
    void                  KeepaliveThreadFunction();

    hid_device*            dev;
    std::string            location;
    std::string            name;
    std::string            fw_version;
    unsigned char          model_id = 0;
    r87pro::Layout         layout;

    RGBControllerInterface* controller_iface = nullptr;

    std::mutex             mutex;
    std::thread            keepalive_thread;
    std::atomic<bool>      keepalive_run{ false };
    bool                   have_last_colors = false;
    std::vector<RGBColor>  last_colors;
    std::chrono::steady_clock::time_point last_update_time;
};
