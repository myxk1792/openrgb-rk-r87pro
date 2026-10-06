/*---------------------------------------------------------*\
| isp_exit.c                                                |
|                                                           |
|   Sends the two SinoWealth ISP commands that make the     |
|   keyboard leave ISP bootloader mode and run its firmware:|
|     feature report 0x05 = 05 55 00 00 00 00  (enable fw)  |
|     feature report 0x05 = 05 5A 00 00 00 00  (reboot)     |
|                                                           |
|   The ISP bootloader has no hidraw node (0-endpoint HID), |
|   so this goes through libusb.  Needs the udev rule in     |
|   ../udev/62-sinowisp.rules (or run as root).             |
|                                                           |
|   Build: cc -O2 -o isp_exit isp_exit.c \                 |
|             $(pkg-config --cflags --libs libusb-1.0)      |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>

#define ISP_VID     0x0603
#define ISP_PID     0x1020
#define CMD_REPORT  0x05
#define CMD_ENABLE  0x55
#define CMD_REBOOT  0x5A

static int send_command(libusb_device_handle* handle, unsigned char command, const char* name)
{
    unsigned char report[6] = { CMD_REPORT, command, 0x00, 0x00, 0x00, 0x00 };

    int result = libusb_control_transfer(handle, 0x21, 0x09, 0x0300 | CMD_REPORT, 0,
                                         report, sizeof(report), 3000);

    printf("  %-16s -> %d %s\n", name, result, result >= 0 ? "" : libusb_error_name(result));

    /* the reboot drops the device off the bus; a disconnect error is expected */
    return((command == CMD_REBOOT) ? 0 : result);
}

int main(void)
{
    libusb_context*       context = NULL;
    libusb_device_handle* handle;

    libusb_init(&context);

    handle = libusb_open_device_with_vid_pid(context, ISP_VID, ISP_PID);

    if(handle == NULL)
    {
        printf("ISP bootloader %04X:%04X not found - is the keyboard in ISP mode?\n", ISP_VID, ISP_PID);
        libusb_exit(context);
        return(1);
    }

    libusb_set_auto_detach_kernel_driver(handle, 1);

    if(libusb_claim_interface(handle, 0) != 0)
    {
        printf("cannot claim ISP interface\n");
        libusb_close(handle);
        libusb_exit(context);
        return(2);
    }

    send_command(handle, CMD_ENABLE, "enable firmware");
    send_command(handle, CMD_REBOOT, "reboot");

    libusb_close(handle);
    libusb_exit(context);

    printf("done - the keyboard should come back as 258a:019f\n");
    return(0);
}
