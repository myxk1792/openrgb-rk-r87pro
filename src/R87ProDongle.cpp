/*---------------------------------------------------------*\
| R87ProDongle.cpp                                          |
|                                                           |
|   2.4G receiver transport for the Royal Kludge R87 Pro.   |
|   See R87ProDongle.h for the packet layout and           |
|   docs/rf-dongle-protocol.md for where it comes from.     |
|                                                           |
|   This file is part of the RK R87 Pro OpenRGB plugin      |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <cstring>

#include "R87ProDongle.h"

using namespace r87pro;

R87ProDongle::R87ProDongle(hid_device* dev_handle, const std::string& dev_path, const std::string& dev_name,
                           const r87pro::Layout& dev_layout)
{
    dev              = dev_handle;
    location         = dev_path;
    name             = dev_name;
    layout           = dev_layout;
    last_update_time = std::chrono::steady_clock::now();
}

R87ProDongle::~R87ProDongle()
{
    Stop();

    if(dev != nullptr)
    {
        hid_close(dev);
        dev = nullptr;
    }
}

/*---------------------------------------------------------*\
| Packet helpers                                            |
\*---------------------------------------------------------*/
static unsigned char PacketCrc(const unsigned char* pkt)
{
    unsigned int sum = DONGLE_PACKET_LEN;

    for(unsigned int i = 0; i < DONGLE_PACKET_LEN - 2; i++)
    {
        sum += pkt[i];
    }

    return((unsigned char)(sum & 0xFF));
}

void R87ProDongle::BuildPacket(unsigned char* pkt, unsigned char cmd, unsigned char board,
                               unsigned char package_num, unsigned char package_index,
                               unsigned char data_len) const
{
    memset(pkt, 0x00, DONGLE_PACKET_LEN);

    pkt[0] = cmd;
    pkt[1] = (unsigned char)(0x7F & package_num);
    pkt[2] = (unsigned char)(0x7F & package_index);
    pkt[3] = (unsigned char)((0x0F & data_len) | ((board << 4) & 0xF0));
    pkt[DONGLE_PACKET_LEN - 1] = PacketCrc(pkt);
}

bool R87ProDongle::SendPacket(const unsigned char* pkt)
{
    if(dev == nullptr)
    {
        return(false);
    }

    unsigned char buffer[DONGLE_PACKET_LEN + 1];

    buffer[0] = DONGLE_REPORT_ID;
    memcpy(buffer + 1, pkt, DONGLE_PACKET_LEN);

    return(hid_write(dev, buffer, sizeof(buffer)) > 0);
}

int R87ProDongle::RecvPacket(unsigned char* pkt, int timeout_ms)
{
    if(dev == nullptr)
    {
        return(-1);
    }

    unsigned char buffer[64];

    for(int attempt = 0; attempt < 8; attempt++)
    {
        int len = hid_read_timeout(dev, buffer, sizeof(buffer), timeout_ms);

        if(len <= 0)
        {
            return(-1);
        }

        if((len >= (int)DONGLE_PACKET_LEN + 1) && (buffer[0] == DONGLE_REPORT_ID))
        {
            memcpy(pkt, buffer + 1, DONGLE_PACKET_LEN);
            return(len - 1);
        }
        /* ignore keyboard/mouse reports that share the interface */
    }

    return(-1);
}

bool R87ProDongle::SimpleCommand(unsigned char cmd, unsigned char* reply, int timeout_ms)
{
    unsigned char pkt[DONGLE_PACKET_LEN];

    BuildPacket(pkt, cmd, 0, 1, 0, 0);

    for(int attempt = 0; attempt < 8; attempt++)
    {
        if(!SendPacket(pkt))
        {
            return(false);
        }

        for(int wait = 0; wait < 5; wait++)
        {
            unsigned char in[DONGLE_PACKET_LEN];

            if(RecvPacket(in, 60) < 0)
            {
                break;
            }

            if(in[0] == DONGLE_CMD_ACTIVELY_REPORT)
            {
                continue;
            }

            if(in[0] == cmd)
            {
                memcpy(reply, in, DONGLE_PACKET_LEN);
                return(true);
            }
        }
    }

    (void)timeout_ms;
    return(false);
}

/*---------------------------------------------------------*\
| Probe: is a keyboard linked to this receiver, and what    |
| model / firmware is it running?                           |
\*---------------------------------------------------------*/
bool R87ProDongle::Probe()
{
    unsigned char reply[DONGLE_PACKET_LEN];

    if(!SimpleCommand(DONGLE_CMD_GET_DONGLE_STAT, reply, 600))
    {
        return(false);
    }

    if(reply[4] == 0)
    {
        /* receiver answers but no keyboard is linked to it */
        return(false);
    }

    if(SimpleCommand(DONGLE_CMD_GET_PASSWORD, reply, 600))
    {
        /* the reply carries the model id as a 16 bit value at [8..9] */
        model_id = reply[9];

        char version[8];
        snprintf(version, sizeof(version), "%02X%02X", reply[12], reply[13]);
        fw_version = version;
    }

    return(true);
}

void R87ProDongle::SetControllerInterface(RGBControllerInterface* iface)
{
    std::lock_guard<std::mutex> lock(mutex);
    controller_iface = iface;
}

void R87ProDongle::Start()
{
    if(keepalive_run.load())
    {
        return;
    }

    keepalive_run.store(true);
    keepalive_thread = std::thread(&R87ProDongle::KeepaliveThreadFunction, this);
}

void R87ProDongle::Stop()
{
    if(keepalive_run.exchange(false))
    {
        if(keepalive_thread.joinable())
        {
            keepalive_thread.join();
        }
    }
}

std::vector<RGBColor> R87ProDongle::ReadControllerColors() const
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
| Push the whole colour buffer: 126 * 3 bytes, planar, in   |
| 14 byte chunks, each waiting for its acknowledgement.     |
\*---------------------------------------------------------*/
bool R87ProDongle::SendColors(const std::vector<RGBColor>& colors)
{
    if(dev == nullptr)
    {
        return(false);
    }

    unsigned char buffer[r87pro::DONGLE_COLOR_LEN];

    memset(buffer, 0x00, sizeof(buffer));

    unsigned int count = (unsigned int)colors.size();

    if(count > layout.led_count)
    {
        count = layout.led_count;
    }

    if(count > DONGLE_LED_SLOTS)
    {
        count = DONGLE_LED_SLOTS;
    }

    for(unsigned int led = 0; led < count; led++)
    {
        buffer[led]                        = (unsigned char)RGBGetRValue(colors[led]);
        buffer[DONGLE_LED_SLOTS + led]      = (unsigned char)RGBGetGValue(colors[led]);
        buffer[DONGLE_LED_SLOTS * 2 + led]  = (unsigned char)RGBGetBValue(colors[led]);
    }

    const unsigned int package_num = (DONGLE_COLOR_LEN + DONGLE_CHUNK - 1) / DONGLE_CHUNK;

    for(unsigned int index = 0; index < package_num; index++)
    {
        unsigned int offset   = index * DONGLE_CHUNK;
        unsigned int data_len = DONGLE_CHUNK;

        if((offset + data_len) > DONGLE_COLOR_LEN)
        {
            data_len = DONGLE_COLOR_LEN - offset;
        }

        unsigned char pkt[DONGLE_PACKET_LEN];

        BuildPacket(pkt, DONGLE_CMD_SET_LED_COLORS, 0, (unsigned char)package_num,
                    (unsigned char)index, (unsigned char)data_len);
        memcpy(pkt + 4, buffer + offset, data_len);
        pkt[DONGLE_PACKET_LEN - 1] = PacketCrc(pkt);

        bool acked = false;

        for(int retry = 0; (retry < 10) && !acked; retry++)
        {
            if(!SendPacket(pkt))
            {
                return(false);
            }

            for(int wait = 0; wait < 8; wait++)
            {
                unsigned char in[DONGLE_PACKET_LEN];

                if(RecvPacket(in, 60) < 0)
                {
                    break;
                }

                if(in[0] == DONGLE_CMD_ACTIVELY_REPORT)
                {
                    continue;
                }

                if(in[0] == DONGLE_CMD_SET_LED_COLORS)
                {
                    acked = ((in[1] >> 7) == 0);
                    break;
                }
            }
        }

        if(!acked)
        {
            return(false);
        }
    }

    return(true);
}

void R87ProDongle::UpdateLEDs()
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
| A colour frame costs about 0.4 s over this link (27       |
| acknowledged packets), so refresh far less often than     |
| the wired path - just enough to keep the keyboard from    |
| dropping back to its own effect.                          |
\*---------------------------------------------------------*/
void R87ProDongle::KeepaliveThreadFunction()
{
    while(keepalive_run.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if(!keepalive_run.load())
        {
            break;
        }

        std::lock_guard<std::mutex> lock(mutex);

        if(!have_last_colors)
        {
            continue;
        }

        if((std::chrono::steady_clock::now() - last_update_time) > std::chrono::milliseconds(4000))
        {
            SendColors(last_colors);
            last_update_time = std::chrono::steady_clock::now();
        }
    }
}

void R87ProDongle::UpdateLEDsCallback(void* object)
{
    static_cast<R87ProDongle*>(object)->UpdateLEDs();
}

void R87ProDongle::UpdateModeCallback(void* object)
{
    R87ProDongle* device = static_cast<R87ProDongle*>(object);

    int mode = DONGLE_MODE_DIRECT;

    {
        std::lock_guard<std::mutex> lock(device->mutex);

        if(device->controller_iface != nullptr)
        {
            mode = device->controller_iface->GetActiveMode();
        }
    }

    if(mode == DONGLE_MODE_OFF)
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

void R87ProDongle::ConfigureZoneCallback(void* /*object*/, int /*zone*/)
{
}
