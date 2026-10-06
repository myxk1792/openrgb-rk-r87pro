/*---------------------------------------------------------*\
| R87ProDevice.h                                            |
|                                                           |
|   HID driver for the Royal Kludge R87 Pro RGB interface   |
|   (BY Tech / SinoWealth, VID 0x258A PID 0x019F).          |
|                                                           |
|   Protocol (same vendor protocol family as OpenRGB's      |
|   SinowealthKeyboard10c driver):                          |
|     * vendor HID collection, usage page 0xFF00 usage 1    |
|     * feature report 0x06, 519 data bytes                 |
|     * query: {06 82 01 00 01 00 06 ...} -> response[13]   |
|       contains the model ID                               |
|     * direct mode: header {06 08 xx xx 01 xx 7A 01} then  |
|       RGB triplets starting at offset 0x08                |
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
    const unsigned int  VENDOR_ID      = 0x258A;
    const unsigned int  PRODUCT_ID     = 0x019F;
    const unsigned int  USAGE_PAGE     = 0xFF00;
    const unsigned int  USAGE          = 0x0001;
    const int           INTERFACE      = 1;

    const int           MODE_OFF       = 0;
    const int           MODE_DIRECT    = 1;

    const unsigned int  REPORT_SIZE    = 520;   /* report ID + 519 data bytes */
    const unsigned int  COLOR_OFFSET   = 0x08;
    const unsigned int  MAX_LEDS       = (REPORT_SIZE - COLOR_OFFSET) / 3;
}

class R87ProDevice
{
public:
    R87ProDevice(hid_device* dev_handle, const std::string& dev_path, const std::string& dev_name,
                 unsigned char dev_model_id, const r87pro::Layout& dev_layout);
    ~R87ProDevice();

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
    const std::string&    GetName() const     { return(name); }
    const std::string&    GetLocation() const { return(location); }
    const r87pro::Layout& GetLayout() const   { return(layout); }
    unsigned char         GetModelID() const  { return(model_id); }
    unsigned int          GetLEDCount() const { return(layout.led_count); }

    /*-----------------------------------------------------*\
    | Runtime                                               |
    \*-----------------------------------------------------*/
    void SetControllerInterface(RGBControllerInterface* iface);
    void Start();
    void Stop();
    void UpdateLEDs();

private:
    void                  SendColors(const std::vector<RGBColor>& colors);
    std::vector<RGBColor> ReadControllerColors() const;
    void                  KeepaliveThreadFunction();

    hid_device*            dev;
    std::string            location;
    std::string            name;
    unsigned char          model_id;
    r87pro::Layout         layout;

    RGBControllerInterface* controller_iface = nullptr;

    std::mutex             mutex;
    std::thread            keepalive_thread;
    std::atomic<bool>      keepalive_run{ false };
    bool                   have_last_colors = false;
    std::vector<RGBColor>  last_colors;
    std::chrono::steady_clock::time_point last_update_time;
};

/*---------------------------------------------------------*\
| Query the model ID of an already opened device            |
\*---------------------------------------------------------*/
unsigned char R87ProQueryModelID(hid_device* dev);
