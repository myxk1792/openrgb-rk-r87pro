/*---------------------------------------------------------*\
| R87ProDevice.cpp                                          |
|                                                           |
|   HID driver for the Royal Kludge R87 Pro RGB interface.  |
|                                                           |
|   This file is part of the RK R87 Pro OpenRGB plugin      |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <cstring>
#include "R87ProDevice.h"

using namespace r87pro;

/*---------------------------------------------------------*\
| Send the model ID query and read the response             |
\*---------------------------------------------------------*/
unsigned char R87ProQueryModelID(hid_device* dev)
{
    unsigned char command[REPORT_SIZE];
    unsigned char response[REPORT_SIZE];

    memset(command, 0x00, sizeof(command));
    command[0] = 0x06;
    command[1] = 0x82;
    command[2] = 0x01;
    command[4] = 0x01;
    command[6] = 0x06;

    if(hid_send_feature_report(dev, command, REPORT_SIZE) < 0)
    {
        return(0);
    }

    memset(response, 0x00, sizeof(response));
    response[0] = 0x06;

    int read = hid_get_feature_report(dev, response, REPORT_SIZE);

    if(read < 14)
    {
        return(0);
    }

    return(response[13]);
}

R87ProDevice::R87ProDevice(hid_device* dev_handle, const std::string& dev_path, const std::string& dev_name,
                           unsigned char dev_model_id, const r87pro::Layout& dev_layout)
{
    dev              = dev_handle;
    location         = dev_path;
    name             = dev_name;
    model_id         = dev_model_id;
    layout           = dev_layout;
    last_update_time = std::chrono::steady_clock::now();
}

R87ProDevice::~R87ProDevice()
{
    Stop();

    if(dev != nullptr)
    {
        hid_close(dev);
        dev = nullptr;
    }
}

void R87ProDevice::SetControllerInterface(RGBControllerInterface* iface)
{
    std::lock_guard<std::mutex> lock(mutex);
    controller_iface = iface;
}

void R87ProDevice::Start()
{
    if(keepalive_run.load())
    {
        return;
    }

    keepalive_run.store(true);
    keepalive_thread = std::thread(&R87ProDevice::KeepaliveThreadFunction, this);
}

void R87ProDevice::Stop()
{
    if(keepalive_run.exchange(false))
    {
        if(keepalive_thread.joinable())
        {
            keepalive_thread.join();
        }
    }
}

std::vector<RGBColor> R87ProDevice::ReadControllerColors() const
{
    std::vector<RGBColor> colors(layout.led_count, 0);

    if((controller_iface != nullptr) && (layout.led_count > 0))
    {
        RGBColor* zone_colors = controller_iface->GetZoneColorsPointer(0);

        if(zone_colors != nullptr)
        {
            memcpy(colors.data(), zone_colors, layout.led_count * sizeof(RGBColor));
        }
    }

    return(colors);
}

/*---------------------------------------------------------*\
| Write one direct-mode frame to the device                 |
\*---------------------------------------------------------*/
void R87ProDevice::SendColors(const std::vector<RGBColor>& colors)
{
    if(dev == nullptr)
    {
        return;
    }

    unsigned char buffer[REPORT_SIZE];

    memset(buffer, 0x00, sizeof(buffer));

    buffer[0x00] = 0x06;    /* feature report ID */
    buffer[0x01] = 0x08;    /* direct mode       */
    buffer[0x04] = 0x01;
    buffer[0x06] = 0x7A;
    buffer[0x07] = 0x01;

    unsigned int count = (unsigned int)colors.size();

    if(count > MAX_LEDS)
    {
        count = MAX_LEDS;
    }

    for(unsigned int led = 0; led < count; led++)
    {
        buffer[COLOR_OFFSET + led * 3 + 0] = (unsigned char)RGBGetRValue(colors[led]);
        buffer[COLOR_OFFSET + led * 3 + 1] = (unsigned char)RGBGetGValue(colors[led]);
        buffer[COLOR_OFFSET + led * 3 + 2] = (unsigned char)RGBGetBValue(colors[led]);
    }

    hid_send_feature_report(dev, buffer, REPORT_SIZE);
}

void R87ProDevice::UpdateLEDs()
{
    std::vector<RGBColor> colors = ReadControllerColors();

    std::lock_guard<std::mutex> lock(mutex);

    if(colors.size() > 0)
    {
        last_colors      = colors;
        have_last_colors = true;
    }

    last_update_time = std::chrono::steady_clock::now();

    SendColors(have_last_colors ? last_colors : colors);
}

/*---------------------------------------------------------*\
| The firmware drops out of direct mode if it does not get  |
| a steady stream of frames, so resend the last frame once  |
| per second (same workaround as OpenRGB's Sinowealth 010C  |
| driver).                                                  |
\*---------------------------------------------------------*/
void R87ProDevice::KeepaliveThreadFunction()
{
    while(keepalive_run.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        if(!keepalive_run.load())
        {
            break;
        }

        std::lock_guard<std::mutex> lock(mutex);

        if(!have_last_colors)
        {
            continue;
        }

        if((std::chrono::steady_clock::now() - last_update_time) > std::chrono::milliseconds(900))
        {
            SendColors(last_colors);
            last_update_time = std::chrono::steady_clock::now();
        }
    }
}

void R87ProDevice::UpdateLEDsCallback(void* object)
{
    static_cast<R87ProDevice*>(object)->UpdateLEDs();
}

void R87ProDevice::UpdateModeCallback(void* object)
{
    R87ProDevice* device = static_cast<R87ProDevice*>(object);

    int mode = MODE_DIRECT;

    {
        std::lock_guard<std::mutex> lock(device->mutex);

        if(device->controller_iface != nullptr)
        {
            mode = device->controller_iface->GetActiveMode();
        }
    }

    if(mode == MODE_OFF)
    {
        std::vector<RGBColor> black(device->layout.led_count, 0);

        std::lock_guard<std::mutex> lock(device->mutex);

        device->last_colors      = black;
        device->have_last_colors = true;
        device->last_update_time = std::chrono::steady_clock::now();

        device->SendColors(black);
    }
    else
    {
        device->UpdateLEDs();
    }
}

void R87ProDevice::ConfigureZoneCallback(void* /*object*/, int /*zone*/)
{
}
