/*---------------------------------------------------------*\
| r87proctl.c                                               |
|                                                           |
|   Standalone diagnostic / calibration tool for the        |
|   Royal Kludge R87 Pro RGB interface (258A:019F).         |
|                                                           |
|   It talks to the keyboard directly through hidapi and    |
|   does not need OpenRGB, which makes it useful for        |
|   checking permissions, reading the firmware model ID and |
|   finding the LED index of every physical key.            |
|                                                           |
|   Build:  cc -O2 -o r87proctl r87proctl.c \              |
|              $(pkg-config --cflags --libs hidapi-hidraw)  |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <hidapi.h>

#define VID             0x258A
#define PID             0x019F
#define USAGE_PAGE      0xFF00
#define USAGE           0x0001
#define REPORT_SIZE     520
#define COLOR_OFFSET    0x08
#define MAX_LEDS        ((REPORT_SIZE - COLOR_OFFSET) / 3)

static void sleep_ms(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void hexdump(const char* tag, const unsigned char* data, int size)
{
    printf("%s (%d bytes):\n", tag, size);

    for(int i = 0; i < size; i++)
    {
        if((i % 16) == 0)
        {
            printf("  %04X: ", i);
        }

        printf("%02X ", data[i]);

        if((i % 16) == 15)
        {
            printf("\n");
        }
    }

    if((size % 16) != 0)
    {
        printf("\n");
    }
}

/*---------------------------------------------------------*\
| Open the vendor RGB collection.  If the hidraw node is    |
| root-only, hid_open_path fails with a permission error:   |
| install the udev rule shipped next to this tool.          |
\*---------------------------------------------------------*/
static hid_device* open_device(const char** out_path)
{
    struct hid_device_info* list = hid_enumerate(VID, PID);
    struct hid_device_info* cur;
    hid_device*             dev = NULL;

    for(cur = list; cur != NULL; cur = cur->next)
    {
        if((cur->usage_page == USAGE_PAGE) && (cur->usage == USAGE) && (cur->path != NULL))
        {
            if(out_path != NULL)
            {
                *out_path = cur->path;
            }

            dev = hid_open_path(cur->path);
            break;
        }
    }

    if(dev == NULL)
    {
        fprintf(stderr, "cannot open the vendor RGB interface of %04X:%04X\n", VID, PID);

        for(cur = list; cur != NULL; cur = cur->next)
        {
            fprintf(stderr, "  found: path=%s usage_page=0x%04X usage=0x%04X\n",
                    cur->path ? cur->path : "(null)", cur->usage_page, cur->usage);
        }

        if(list == NULL)
        {
            fprintf(stderr, "  no %04X:%04X device found at all\n", VID, PID);
        }

        fprintf(stderr, "If the device is listed above, install the udev rule:\n"
                        "  sudo install -m 0644 udev/61-openrgb-rk-r87pro.rules /etc/udev/rules.d/\n"
                        "  sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw --action=change\n");
    }

    if(list != NULL)
    {
        hid_free_enumeration(list);
    }

    return(dev);
}

static int send_colors(hid_device* dev, const unsigned char* rgb, unsigned int leds)
{
    unsigned char buffer[REPORT_SIZE];
    unsigned int  count = leds;

    if(count > MAX_LEDS)
    {
        count = MAX_LEDS;
    }

    memset(buffer, 0x00, sizeof(buffer));
    buffer[0x00] = 0x06;
    buffer[0x01] = 0x08;
    buffer[0x04] = 0x01;
    buffer[0x06] = 0x7A;
    buffer[0x07] = 0x01;

    if((rgb != NULL) && (count > 0))
    {
        memcpy(&buffer[COLOR_OFFSET], rgb, count * 3);
    }

    return(hid_send_feature_report(dev, buffer, REPORT_SIZE));
}

static unsigned char query_model_id(hid_device* dev)
{
    unsigned char command[REPORT_SIZE];
    unsigned char response[REPORT_SIZE];
    int           read;

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

    read = hid_get_feature_report(dev, response, REPORT_SIZE);

    if(read < 14)
    {
        return(0);
    }

    hexdump("model query response", response, 32);

    return(response[13]);
}

static int parse_hex_color(const char* text, unsigned char* rgb)
{
    unsigned int r, g, b;

    while((*text == '#') || (*text == '0' && (text[1] == 'x' || text[1] == 'X')))
    {
        text += (*text == '#') ? 1 : 2;
    }

    if(strlen(text) != 6)
    {
        return(-1);
    }

    if(sscanf(text, "%2x%2x%2x", &r, &g, &b) != 3)
    {
        return(-1);
    }

    rgb[0] = (unsigned char)r;
    rgb[1] = (unsigned char)g;
    rgb[2] = (unsigned char)b;

    return(0);
}

static void usage(const char* argv0)
{
    printf("usage: %s <command> [args]\n\n", argv0);
    printf("  info                          list devices, show model ID and feature report 0x06\n");
    printf("  off                           turn all LEDs off\n");
    printf("  fill <RRGGBB>                 set every LED to a color until Ctrl+C\n");
    printf("  test <led> [RRGGBB] [secs]    light a single LED index (default red, 5 s)\n");
    printf("  sweep [from] [to] [secs]      light LED indices one by one (default 0..112, 1.5 s each)\n");
    printf("  multi <led:RRGGBB> ... <secs> light several LEDs at once for calibration\n");
    printf("  regsweep [secs]               sweep candidate mode packets then hold blue\n");
    printf("  setall <RRGGBB>               send one full-board colour frame and exit\n");
    printf("  cmd <byte> [byte...]          send a raw feature report / command and dump\n");
    printf("                                the report 0x06 response (first 16 bytes)\n");
    printf("  explore                       send vendor commands on report 0x05 and dump\n");
    printf("                                the responses of reports 0x05 / 0x06\n");
    printf("  offtest2 [secs]               cycle mode-config candidates (0x14/0x15)\n");
    printf("  offtest [secs]                cycle candidate frames looking for one that\n");
    printf("                                really blanks the unused LEDs (default 12 s each)\n");
    printf("  raw <byte> [byte...]          send a raw feature report 0x06 payload\n");
    printf("\n");
    printf("While a color command runs the frame is resent continuously so the\n");
    printf("firmware stays in direct mode.  Press Ctrl+C to stop.\n");
}

int main(int argc, char** argv)
{
    hid_device* dev = NULL;
    const char* path = NULL;

    if(argc < 2)
    {
        usage(argv[0]);
        return(1);
    }

    hid_init();

    if(strcmp(argv[1], "info") == 0)
    {
        struct hid_device_info* list = hid_enumerate(VID, PID);
        struct hid_device_info* cur;

        printf("=== %04X:%04X HID collections ===\n", VID, PID);

        for(cur = list; cur != NULL; cur = cur->next)
        {
            printf("path=%s iface=%d usage_page=0x%04X usage=0x%04X product=%ls\n",
                   cur->path ? cur->path : "(null)", cur->interface_number,
                   cur->usage_page, cur->usage, cur->product_string);
        }

        if(list != NULL)
        {
            hid_free_enumeration(list);
        }

        dev = open_device(&path);

        if(dev == NULL)
        {
            return(2);
        }

        printf("\nopened %s\n", path);

        unsigned char model = query_model_id(dev);
        printf("model ID: 0x%02X (%u)\n", model, model);

        unsigned char feature[REPORT_SIZE];
        memset(feature, 0x00, sizeof(feature));
        feature[0] = 0x06;
        int read = hid_get_feature_report(dev, feature, REPORT_SIZE);
        printf("feature report 0x06 read: %d bytes\n", read);

        if(read > 0)
        {
            hexdump("feature 0x06", feature, 32);
        }

        hid_close(dev);
    }
    else if((strcmp(argv[1], "off") == 0)
         || (strcmp(argv[1], "fill") == 0)
         || (strcmp(argv[1], "test") == 0)
         || (strcmp(argv[1], "sweep") == 0)
         || (strcmp(argv[1], "multi") == 0)
         || (strcmp(argv[1], "offtest") == 0)
         || (strcmp(argv[1], "offtest2") == 0)
         || (strcmp(argv[1], "explore") == 0)
         || (strcmp(argv[1], "cmd") == 0)
         || (strcmp(argv[1], "setall") == 0)
         || (strcmp(argv[1], "regsweep") == 0)
         || (strcmp(argv[1], "raw") == 0))
    {
        unsigned char* frame = (unsigned char*)calloc(MAX_LEDS, 3);

        if(frame == NULL)
        {
            fprintf(stderr, "out of memory\n");
            return(1);
        }

        if(strcmp(argv[1], "raw") == 0)
        {
            unsigned char buffer[REPORT_SIZE];
            int           size = 0;

            memset(buffer, 0x00, sizeof(buffer));
            buffer[0] = 0x06;

            for(int i = 2; i < argc; i++)
            {
                buffer[size] = (unsigned char)strtoul(argv[i], NULL, 16);
                size++;
            }

            dev = open_device(&path);

            if(dev == NULL)
            {
                free(frame);
                return(2);
            }

            printf("sent %d bytes -> %d\n", size, hid_send_feature_report(dev, buffer, size));
            hid_close(dev);
            hid_exit();
            free(frame);
            return(0);
        }

        dev = open_device(&path);

        if(dev == NULL)
        {
            free(frame);
            return(2);
        }

        if(strcmp(argv[1], "off") == 0)
        {
            send_colors(dev, frame, MAX_LEDS);
            printf("all LEDs off\n");
        }
        else if(strcmp(argv[1], "fill") == 0)
        {
            unsigned char rgb[3] = { 255, 0, 0 };
            unsigned int  leds = MAX_LEDS;

            if(argc < 3) { usage(argv[0]); free(frame); hid_close(dev); hid_exit(); return(1); }

            if(parse_hex_color(argv[2], rgb) != 0)
            {
                fprintf(stderr, "invalid color '%s'\n", argv[2]);
                free(frame);
                hid_close(dev);
                hid_exit();
                return(1);
            }

            for(unsigned int i = 0; i < leds; i++)
            {
                frame[i * 3 + 0] = rgb[0];
                frame[i * 3 + 1] = rgb[1];
                frame[i * 3 + 2] = rgb[2];
            }

            printf("holding #%02X%02X%02X on all LEDs, Ctrl+C to stop\n", rgb[0], rgb[1], rgb[2]);

            for(;;)
            {
                send_colors(dev, frame, leds);
                sleep_ms(400);
            }
        }
        else if(strcmp(argv[1], "test") == 0)
        {
            unsigned char rgb[3] = { 255, 0, 0 };
            int           led;
            double        seconds = 5.0;

            if(argc < 3) { usage(argv[0]); free(frame); hid_close(dev); hid_exit(); return(1); }

            led = (int)strtol(argv[2], NULL, 0);

            if(argc > 3) { parse_hex_color(argv[3], rgb); }
            if(argc > 4) { seconds = atof(argv[4]); }

            if((led < 0) || (led >= (int)MAX_LEDS))
            {
                fprintf(stderr, "LED index out of range (0..%d)\n", MAX_LEDS - 1);
                free(frame);
                hid_close(dev);
                hid_exit();
                return(1);
            }

            frame[led * 3 + 0] = rgb[0];
            frame[led * 3 + 1] = rgb[1];
            frame[led * 3 + 2] = rgb[2];

            printf("LED %d -> #%02X%02X%02X for %.1f s\n", led, rgb[0], rgb[1], rgb[2], seconds);

            for(double elapsed = 0.0; elapsed < seconds; elapsed += 0.4)
            {
                send_colors(dev, frame, MAX_LEDS);
                sleep_ms(400);
            }

            memset(frame, 0x00, (size_t)MAX_LEDS * 3);
            send_colors(dev, frame, MAX_LEDS);
        }
        else if(strcmp(argv[1], "regsweep") == 0)
        {
            double seconds = (argc > 2) ? atof(argv[2]) : 12.0;

            unsigned char regs[] =
            {
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x0B, 0x0C
            };
            const int reg_count = (int)(sizeof(regs) / sizeof(regs[0]));

            unsigned char colours[][3] =
            {
                { 255,   0,   0 }, {   0, 255,   0 }, {   0,   0, 255 }, { 255, 255,   0 },
                {   0, 255, 255 }, { 255,   0, 255 }, { 255, 128,   0 }, { 255, 255, 255 },
                { 128,   0,   0 }, {   0, 128,   0 }, {   0,   0, 128 }, { 128, 128, 128 },
            };

            for(int i = 0; i < reg_count; i++)
            {
                printf("  候选 %d: reg=0x%02X  -> LED0 = #%02X%02X%02X\n", i + 1, regs[i],
                       colours[i][0], colours[i][1], colours[i][2]);
            }
            printf("\n每个候选先发一条 mode 包（len=0x4B，全 0），随后持续发送“全蓝 + LED0 候选色” %.0f 秒。\n", seconds);
            fflush(stdout);

            for(;;)
            {
                for(int i = 0; i < reg_count; i++)
                {
                    unsigned char mode[REPORT_SIZE];
                    unsigned char frame_all[REPORT_SIZE];

                    printf("  -> 候选 %d reg=0x%02X\n", i + 1, regs[i]);
                    fflush(stdout);

                    memset(mode, 0x00, sizeof(mode));
                    mode[0] = 0x06;
                    mode[1] = regs[i];
                    mode[4] = 0x01;
                    mode[6] = 0x4B;
                    mode[7] = 0x00;

                    hid_send_feature_report(dev, mode, REPORT_SIZE);

                    memset(frame_all, 0x00, sizeof(frame_all));
                    frame_all[0] = 0x06;
                    frame_all[1] = 0x08;
                    frame_all[4] = 0x01;
                    frame_all[6] = 0x7A;
                    frame_all[7] = 0x01;

                    for(unsigned int led = 0; led < MAX_LEDS; led++)
                    {
                        frame_all[COLOR_OFFSET + led * 3 + 0] = 0x00;
                        frame_all[COLOR_OFFSET + led * 3 + 1] = 0x00;
                        frame_all[COLOR_OFFSET + led * 3 + 2] = 0xFF;
                    }

                    frame_all[COLOR_OFFSET + 0] = colours[i][0];
                    frame_all[COLOR_OFFSET + 1] = colours[i][1];
                    frame_all[COLOR_OFFSET + 2] = colours[i][2];

                    for(double elapsed = 0.0; elapsed < seconds; elapsed += 0.4)
                    {
                        hid_send_feature_report(dev, frame_all, REPORT_SIZE);
                        sleep_ms(400);
                    }
                }
            }
        }
        else if(strcmp(argv[1], "setall") == 0)
        {
            unsigned char rgb[3] = { 255, 0, 0 };

            if(argc > 2)
            {
                parse_hex_color(argv[2], rgb);
            }

            for(int i = 0; i < MAX_LEDS; i++)
            {
                frame[i * 3 + 0] = rgb[0];
                frame[i * 3 + 1] = rgb[1];
                frame[i * 3 + 2] = rgb[2];
            }

            dev = open_device(&path);

            if(dev == NULL)
            {
                free(frame);
                return(2);
            }

            printf("setall #%02X%02X%02X -> %d\n", rgb[0], rgb[1], rgb[2], send_colors(dev, frame, MAX_LEDS));
        }
        else if(strcmp(argv[1], "cmd") == 0)
        {
            unsigned char buffer[REPORT_SIZE];
            int size = 0;

            memset(buffer, 0x00, sizeof(buffer));

            for(int i = 2; (i < argc) && (size < REPORT_SIZE); i++)
            {
                buffer[size] = (unsigned char)strtoul(argv[i], NULL, 16);
                size++;
            }

            dev = open_device(&path);

            if(dev == NULL)
            {
                free(frame);
                return(2);
            }

            printf("send %d bytes -> %d\n", size, hid_send_feature_report(dev, buffer, size));

            unsigned char reply[REPORT_SIZE];
            memset(reply, 0x00, sizeof(reply));
            reply[0] = 0x06;

            int read = hid_get_feature_report(dev, reply, REPORT_SIZE);

            printf("report 0x06 response (%d bytes): ", read);

            for(int i = 0; i < (read > 0 ? read : 0) && i < 16; i++)
            {
                printf("%02X ", reply[i]);
            }
            printf("\n");
        }
        else if(strcmp(argv[1], "explore") == 0)
        {
            dev = open_device(&path);

            if(dev == NULL)
            {
                free(frame);
                return(2);
            }

            static const unsigned char commands[][6] =
            {
                { 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 },
                { 0x05, 0x01, 0xAA, 0xBB, 0x2F, 0x3E },
                { 0x05, 0x83, 0x00, 0x00, 0x00, 0x00 },
                { 0x05, 0x88, 0xA8, 0x00, 0x40, 0x00 },
                { 0x05, 0x89, 0xAC, 0x00, 0x40, 0x00 },
                { 0x05, 0x89, 0xB0, 0x00, 0x40, 0x00 },
                { 0x05, 0x89, 0xB4, 0x00, 0x40, 0x00 },
                { 0x06, 0x82, 0x01, 0x00, 0x01, 0x00 },
            };
            const int command_count = (int)(sizeof(commands) / sizeof(commands[0]));

            unsigned char reply[REPORT_SIZE];
            int           read;

            /*-----------------------------------------------------*\
            | Baseline: what do the reports contain right now?      |
            \*-----------------------------------------------------*/
            memset(reply, 0x00, sizeof(reply));
            reply[0] = 0x05;
            read = hid_get_feature_report(dev, reply, 6);
            printf("feature 0x05 idle (ret %d): ", read);

            for(int i = 0; i < (read > 0 ? read : 0) && i < 6; i++)
            {
                printf("%02X ", reply[i]);
            }
            printf("\n");

            for(int c = 0; c < command_count; c++)
            {
                printf("\n=== send report 0x05: ");
                for(int i = 0; i < 6; i++)
                {
                    printf("%02X ", commands[c][i]);
                }
                printf("\n");

                printf("  send -> %d\n", hid_send_feature_report(dev, commands[c], 6));

                memset(reply, 0x00, sizeof(reply));
                reply[0] = 0x05;
                read = hid_get_feature_report(dev, reply, 6);
                printf("  report 0x05 (ret %d): ", read);

                for(int i = 0; i < (read > 0 ? read : 0) && i < 6; i++)
                {
                    printf("%02X ", reply[i]);
                }
                printf("\n");

                memset(reply, 0x00, sizeof(reply));
                reply[0] = 0x06;
                read = hid_get_feature_report(dev, reply, REPORT_SIZE);
                printf("  report 0x06 (ret %d): ", read);

                for(int i = 0; i < (read > 0 ? read : 0) && i < 32; i++)
                {
                    printf("%02X ", reply[i]);
                }
                printf("\n");
            }

            /*-----------------------------------------------------*\
            | Restore a plain black frame so the keyboard is not    |
            | left in a strange state.                              |
            \*-----------------------------------------------------*/
            memset(frame, 0x00, (size_t)MAX_LEDS * 3);
            send_colors(dev, frame, MAX_LEDS);
        }
        else if(strcmp(argv[1], "offtest2") == 0)
        {
            double seconds = (argc > 2) ? atof(argv[2]) : 12.0;

            /*-----------------------------------------------------*\
            | Candidates based on OpenRGB's Sinowealth PID 0016     |
            | finding: OEM software switches the keyboard to per-key |
            | custom mode by writing 0x0F to config byte 0x15 and    |
            | 0x01 to config byte 0x14.                              |
            \*-----------------------------------------------------*/
            struct candidate
            {
                const char*   label;
                const char*   pre1;
                const char*   pre2;
                unsigned char hdr[8];
                int           idx1;         /* extra byte position or -1 */
                unsigned char val1;
                int           idx2;
                unsigned char val2;
            };

            static const struct candidate candidates[] =
            {
                { "baseline (header only)",                       NULL, NULL, { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, -1, 0, -1, 0 },
                { "byte 0x14=01 0x15=0F",                         NULL, NULL, { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, 0x14, 0x01, 0x15, 0x0F },
                { "pre 83, byte 0x14=01 0x15=0F",                 "83", NULL, { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, 0x14, 0x01, 0x15, 0x0F },
                { "pre init+83, byte 0x14=01 0x15=0F",            "init", "83", { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, 0x14, 0x01, 0x15, 0x0F },
                { "header byte1=0x0F",                            NULL, NULL, { 0x06, 0x0F, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, -1, 0, -1, 0 },
                { "pre init, byte 0x14=01 0x15=0F",               "init", NULL, { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, 0x14, 0x01, 0x15, 0x0F },
                { "byte 0x14=01 only",                            NULL, NULL, { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, 0x14, 0x01, -1, 0 },
                { "byte 0x15=0F only",                            NULL, NULL, { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, 0x15, 0x0F, -1, 0 },
            };
            const int candidate_count = (int)(sizeof(candidates) / sizeof(candidates[0]));

            const unsigned char colours[][3] =
            {
                { 255,   0,   0 }, {   0, 255,   0 }, {   0,   0, 255 }, { 255, 255,   0 },
                {   0, 255, 255 }, { 255,   0, 255 }, { 255, 128,   0 }, { 255, 255, 255 },
            };

            for(int i = 0; i < candidate_count; i++)
            {
                printf("  候选 %d: %-34s -> LED0 = #%02X%02X%02X\n", i + 1, candidates[i].label,
                       colours[i][0], colours[i][1], colours[i][2]);
            }
            printf("\n循环播放，每个候选 %.0f 秒。若某个候选让其余按键全灭，请记住对应颜色。\n", seconds);
            fflush(stdout);

            for(;;)
            {
                for(int i = 0; i < candidate_count; i++)
                {
                    unsigned char buffer[REPORT_SIZE];

                    printf("  -> 候选 %d (%s) LED0 #%02X%02X%02X\n", i + 1, candidates[i].label,
                           colours[i][0], colours[i][1], colours[i][2]);
                    fflush(stdout);

                    if(candidates[i].pre1 != NULL)
                    {
                        unsigned char pre[6];

                        if(strcmp(candidates[i].pre1, "init") == 0)
                        {
                            memcpy(pre, (unsigned char[]){ 0x05, 0x01, 0xAA, 0xBB, 0x2F, 0x3E }, 6);
                        }
                        else
                        {
                            memcpy(pre, (unsigned char[]){ 0x05, 0x83, 0x00, 0x00, 0x00, 0x00 }, 6);
                        }

                        hid_send_feature_report(dev, pre, 6);
                    }

                    if(candidates[i].pre2 != NULL)
                    {
                        unsigned char pre[6];
                        memcpy(pre, (unsigned char[]){ 0x05, 0x83, 0x00, 0x00, 0x00, 0x00 }, 6);
                        hid_send_feature_report(dev, pre, 6);
                    }

                    memset(buffer, 0x00, sizeof(buffer));
                    memcpy(buffer, candidates[i].hdr, 8);

                    if(candidates[i].idx1 >= 0)
                    {
                        buffer[candidates[i].idx1] = candidates[i].val1;
                    }
                    if(candidates[i].idx2 >= 0)
                    {
                        buffer[candidates[i].idx2] = candidates[i].val2;
                    }

                    buffer[COLOR_OFFSET + 0] = colours[i][0];
                    buffer[COLOR_OFFSET + 1] = colours[i][1];
                    buffer[COLOR_OFFSET + 2] = colours[i][2];

                    for(double elapsed = 0.0; elapsed < seconds; elapsed += 0.4)
                    {
                        hid_send_feature_report(dev, buffer, REPORT_SIZE);
                        sleep_ms(400);
                    }
                }
            }
        }
        else if(strcmp(argv[1], "offtest") == 0)
        {
            double seconds = (argc > 2) ? atof(argv[2]) : 12.0;

            /*-----------------------------------------------------*\
            | Each candidate lights LED 0 in a distinct colour and   |
            | leaves every other LED black.  If a candidate makes    |
            | the rest of the keyboard go dark (instead of showing   |
            | the firmware effect), the one lit key tells which      |
            | colour/candidate worked.                               |
            \*-----------------------------------------------------*/
            struct
            {
                const char*   label;
                unsigned char hdr[8];
                const char*   pre;      /* optional 6 byte report 0x05 command */
            } variants[] =
            {
                { "buf[1]=0x08 (current)                          ", { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, NULL },
                { "buf[1]=0x01                                    ", { 0x06, 0x01, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, NULL },
                { "buf[1]=0x00                                    ", { 0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, NULL },
                { "buf[4]=0x00                                    ", { 0x06, 0x08, 0x00, 0x00, 0x00, 0x00, 0x7A, 0x01 }, NULL },
                { "model query first, then current frame          ", { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, "query" },
                { "report 0x05 {05 83 00 00 00 00} first          ", { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, "modes" },
                { "report 0x05 {05 01 AA BB 2F 3E} first          ", { 0x06, 0x08, 0x00, 0x00, 0x01, 0x00, 0x7A, 0x01 }, "init"  },
            };
            const unsigned char variant_colours[][3] =
            {
                { 255,   0,   0 },   /* red     */
                {   0, 255,   0 },   /* green   */
                {   0,   0, 255 },   /* blue    */
                { 255, 255,   0 },   /* yellow  */
                {   0, 255, 255 },   /* cyan    */
                { 255,   0, 255 },   /* magenta */
                { 255, 128,   0 },   /* orange  */
            };
            const int variant_count = (int)(sizeof(variants) / sizeof(variants[0]));

            for(int i = 0; i < variant_count; i++)
            {
                printf("  候选 %d: %s -> LED0 = #%02X%02X%02X\n", i + 1, variants[i].label,
                       variant_colours[i][0], variant_colours[i][1], variant_colours[i][2]);
            }
            printf("\n循环播放，每个候选 %.0f 秒。观察哪一个候选让其余按键全灭。\n", seconds);
            fflush(stdout);

            for(;;)
            {
                for(int i = 0; i < variant_count; i++)
                {
                    unsigned char buffer[REPORT_SIZE];
                    unsigned char pre[6];

                    printf("  -> 候选 %d (%s) LED0 #%02X%02X%02X\n", i + 1, variants[i].label,
                           variant_colours[i][0], variant_colours[i][1], variant_colours[i][2]);
                    fflush(stdout);

                    if(variants[i].pre != NULL)
                    {
                        memset(pre, 0x00, sizeof(pre));

                        if(strcmp(variants[i].pre, "query") == 0)
                        {
                            memcpy(pre, (unsigned char[]){ 0x06, 0x82, 0x01, 0x00, 0x01, 0x00 }, 6);
                        }
                        else if(strcmp(variants[i].pre, "modes") == 0)
                        {
                            memcpy(pre, (unsigned char[]){ 0x05, 0x83, 0x00, 0x00, 0x00, 0x00 }, 6);
                        }
                        else
                        {
                            memcpy(pre, (unsigned char[]){ 0x05, 0x01, 0xAA, 0xBB, 0x2F, 0x3E }, 6);
                        }

                        hid_send_feature_report(dev, pre, sizeof(pre));
                    }

                    memset(buffer, 0x00, sizeof(buffer));
                    memcpy(buffer, variants[i].hdr, 8);
                    buffer[COLOR_OFFSET + 0] = variant_colours[i][0];
                    buffer[COLOR_OFFSET + 1] = variant_colours[i][1];
                    buffer[COLOR_OFFSET + 2] = variant_colours[i][2];

                    for(double elapsed = 0.0; elapsed < seconds; elapsed += 0.4)
                    {
                        hid_send_feature_report(dev, buffer, REPORT_SIZE);
                        sleep_ms(400);
                    }
                }
            }
        }
        else if(strcmp(argv[1], "multi") == 0)
        {
            double seconds = 20.0;
            int    count   = 0;

            for(int i = 2; i < argc; i++)
            {
                char* colon = strchr(argv[i], ':');

                if(colon == NULL)
                {
                    seconds = atof(argv[i]);
                    break;
                }

                *colon = '\0';

                int           led = (int)strtol(argv[i], NULL, 0);
                unsigned char rgb[3];

                if((led < 0) || (led >= (int)MAX_LEDS) || (parse_hex_color(colon + 1, rgb) != 0))
                {
                    fprintf(stderr, "bad entry '%s:%s'\n", argv[i], colon + 1);
                    free(frame);
                    hid_close(dev);
                    hid_exit();
                    return(1);
                }

                frame[led * 3 + 0] = rgb[0];
                frame[led * 3 + 1] = rgb[1];
                frame[led * 3 + 2] = rgb[2];
                count++;

                printf("  LED %-3d = #%02X%02X%02X\n", led, rgb[0], rgb[1], rgb[2]);
            }

            printf("holding %d LEDs for %.0f s, Ctrl+C to stop\n", count, seconds);

            for(double elapsed = 0.0; elapsed < seconds; elapsed += 0.4)
            {
                send_colors(dev, frame, MAX_LEDS);
                sleep_ms(400);
            }

            memset(frame, 0x00, (size_t)MAX_LEDS * 3);
            send_colors(dev, frame, MAX_LEDS);
        }
        else if(strcmp(argv[1], "sweep") == 0)
        {
            int    from    = (argc > 2) ? (int)strtol(argv[2], NULL, 0) : 0;
            int    to      = (argc > 3) ? (int)strtol(argv[3], NULL, 0) : 112;
            double seconds = (argc > 4) ? atof(argv[4]) : 1.5;

            printf("sweeping LED %d..%d, each %.1f s\n", from, to, seconds);

            for(int led = from; led <= to; led++)
            {
                if((led < 0) || (led >= (int)MAX_LEDS))
                {
                    continue;
                }

                memset(frame, 0x00, (size_t)MAX_LEDS * 3);
                frame[led * 3 + 0] = 255;
                frame[led * 3 + 1] = 0;
                frame[led * 3 + 2] = 0;

                printf("LED %d\n", led);
                fflush(stdout);

                for(double elapsed = 0.0; elapsed < seconds; elapsed += 0.3)
                {
                    send_colors(dev, frame, MAX_LEDS);
                    sleep_ms(300);
                }
            }

            memset(frame, 0x00, (size_t)MAX_LEDS * 3);
            send_colors(dev, frame, MAX_LEDS);
        }

        hid_close(dev);
        free(frame);
    }
    else
    {
        usage(argv[0]);
        hid_exit();
        return(1);
    }

    hid_exit();

    return(0);
}
