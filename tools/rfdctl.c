/*---------------------------------------------------------*\
| rfdctl.c                                                  |
|                                                           |
|   2.4G dongle control tool for the Royal Kludge R87 Pro.  |
|                                                           |
|   Speaks the packet protocol used by the vendor web app   |
|   on the receiver's usage page 0xFF02 / usage 2           |
|   collection: 19-byte report 0x13, up to 14 payload bytes |
|   per packet, CRC = (19 + sum(byte 0..16)) & 0xFF.        |
|                                                           |
|   Commands:                                               |
|     info              device info (no traffic)            |
|     status            GetDongleStatus  (7)                |
|     password          GetPassword      (5)                |
|     colors [board]    GetLedColors     (66)               |
|     save <file>       dump the colour buffer to a file    |
|     load <file>       write a colour buffer back          |
|     fill <RRGGBB>     set every LED    (2, chunked+acked) |
|                                                           |
|   The colour buffer is 126 * 3 bytes, planar: 126 red     |
|   bytes, then 126 green, then 126 blue.                   |
|                                                           |
|   Build: cc -O2 -o rfdctl rfdctl.c                        |
\*---------------------------------------------------------*/

#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REPORT_ID   0x13
#define PKT_LEN     19
#define LED_COUNT   126
#define COLOR_LEN   (LED_COUNT * 3)

#define CMD_SET_LED_COLORS   2
#define CMD_GET_PASSWORD     5
#define CMD_GET_DONGLE_STAT  7
#define CMD_ACTIVELY_REPORT 10
#define CMD_GET_LED_COLORS  66

static int g_fd = -1;

static void dump(const char* tag, const uint8_t* data, int len)
{
    printf("  %-14s:", tag);

    for(int i = 0; i < len; i++)
    {
        printf(" %02X", data[i]);
    }

    printf("\n");
}

static uint8_t packet_crc(const uint8_t* pkt)
{
    uint32_t sum = PKT_LEN;

    for(int i = 0; i < PKT_LEN - 2; i++)
    {
        sum += pkt[i];
    }

    return((uint8_t)(sum & 0xFF));
}

static void packet_build(uint8_t* pkt, uint8_t cmd, uint8_t board, uint8_t package_num,
                         uint8_t package_index, uint8_t data_len)
{
    memset(pkt, 0, PKT_LEN);

    pkt[0] = cmd;
    pkt[1] = (uint8_t)(0x7F & package_num);
    pkt[2] = (uint8_t)(0x7F & package_index);
    pkt[3] = (uint8_t)((0x0F & data_len) | ((board << 4) & 0xF0));
    pkt[PKT_LEN - 1] = packet_crc(pkt);
}

static int packet_send(const uint8_t* pkt)
{
    uint8_t buf[PKT_LEN + 1];

    buf[0] = REPORT_ID;
    memcpy(buf + 1, pkt, PKT_LEN);

    if(ioctl(g_fd, HIDIOCSOUTPUT(sizeof(buf)), buf) < 0)
    {
        fprintf(stderr, "HIDIOCSOUTPUT failed: %s\n", strerror(errno));
        return(-1);
    }

    return(0);
}

static int packet_recv(uint8_t* pkt, int timeout_ms)
{
    uint8_t buf[128];

    for(;;)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(g_fd, &set);

        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };

        if(select(g_fd + 1, &set, NULL, NULL, &tv) <= 0)
        {
            return(-1);
        }

        int len = read(g_fd, buf, sizeof(buf));

        if(len < 0)
        {
            if(errno == EAGAIN || errno == EINTR)
            {
                continue;
            }

            return(-1);
        }

        if(len >= PKT_LEN + 1 && buf[0] == REPORT_ID)
        {
            memcpy(pkt, buf + 1, PKT_LEN);
            return(len - 1);
        }

        if(len >= PKT_LEN)
        {
            memcpy(pkt, buf, PKT_LEN);
            return(len);
        }
    }
}

static int simple_command(uint8_t cmd, uint8_t board, uint8_t* reply)
{
    uint8_t pkt[PKT_LEN];

    packet_build(pkt, cmd, board, 1, 0, 0);

    for(int attempt = 0; attempt < 8; attempt++)
    {
        if(packet_send(pkt) != 0)
        {
            return(-1);
        }

        for(int wait = 0; wait < 5; wait++)
        {
            uint8_t in[PKT_LEN];

            if(packet_recv(in, 60) < 0)
            {
                break;
            }

            if(in[0] == CMD_ACTIVELY_REPORT)
            {
                printf("  [dongle] actively report: type=%u value=%u\n", in[4], in[5]);
                continue;
            }

            if(in[0] == cmd)
            {
                memcpy(reply, in, PKT_LEN);
                return(0);
            }
        }
    }

    return(-1);
}

static int read_colors(uint8_t* buf, uint8_t board)
{
    uint8_t pkt[PKT_LEN];
    int     have = 0;

    memset(buf, 0, COLOR_LEN);
    packet_build(pkt, CMD_GET_LED_COLORS, board, 1, 0, 0);

    if(packet_send(pkt) != 0)
    {
        return(-1);
    }

    for(int i = 0; i < 200; i++)
    {
        uint8_t in[PKT_LEN];

        if(packet_recv(in, 300) < 0)
        {
            break;
        }

        if(in[0] == CMD_ACTIVELY_REPORT)
        {
            continue;
        }

        if(in[0] != CMD_GET_LED_COLORS)
        {
            continue;
        }

        uint8_t package_num   = in[1] & 0x7F;
        uint8_t package_index = in[2] & 0x7F;
        uint8_t data_len      = in[3] & 0x0F;

        if(have + data_len <= COLOR_LEN)
        {
            memcpy(buf + have, in + 4, data_len);
            have += data_len;
        }

        if(package_num > 0 && package_index >= package_num - 1)
        {
            break;
        }
    }

    return(have);
}

static int write_colors(const uint8_t* colors, uint8_t board)
{
    const int chunk       = 14;
    const int package_num = (COLOR_LEN + chunk - 1) / chunk;

    for(int index = 0; index < package_num; index++)
    {
        int     offset   = index * chunk;
        int     data_len = (offset + chunk > COLOR_LEN) ? (COLOR_LEN - offset) : chunk;
        uint8_t pkt[PKT_LEN];
        int     acked = 0;

        packet_build(pkt, CMD_SET_LED_COLORS, board, (uint8_t)package_num, (uint8_t)index,
                     (uint8_t)data_len);
        memcpy(pkt + 4, colors + offset, data_len);
        pkt[PKT_LEN - 1] = packet_crc(pkt);

        for(int retry = 0; retry < 10 && !acked; retry++)
        {
            if(packet_send(pkt) != 0)
            {
                return(-1);
            }

            for(int wait = 0; wait < 8; wait++)
            {
                uint8_t in[PKT_LEN];

                if(packet_recv(in, 60) < 0)
                {
                    break;
                }

                if(in[0] == CMD_ACTIVELY_REPORT)
                {
                    continue;
                }

                if(in[0] == CMD_SET_LED_COLORS)
                {
                    acked = ((in[1] >> 7) == 0);
                    break;
                }
            }
        }

        if(!acked)
        {
            printf("  packet %d/%d NOT acked\n", index + 1, package_num);
            return(-1);
        }

        if(index % 6 == 0 || index == package_num - 1)
        {
            printf("  packet %d/%d ok\n", index + 1, package_num);
        }
    }

    return(0);
}

static void print_colors(const uint8_t* buf, int verbose)
{
    for(int led = 0; led < LED_COUNT; led++)
    {
        uint8_t r = buf[led];
        uint8_t g = buf[LED_COUNT + led];
        uint8_t b = buf[LED_COUNT * 2 + led];

        if(verbose || led < 16 || (r | g | b) != 0)
        {
            printf("  LED %3d: #%02X%02X%02X\n", led, r, g, b);
        }
    }
}

static int cmd_info(void)
{
    struct hidraw_devinfo info;
    char                  name[128] = { 0 };

    if(ioctl(g_fd, HIDIOCGRAWINFO, &info) == 0)
    {
        printf("  bustype=%04X vendor=%04X product=%04X\n", info.bustype, info.vendor, info.product);
    }

    if(ioctl(g_fd, HIDIOCGRAWNAME(sizeof(name)), name) >= 0)
    {
        printf("  name=%s\n", name);
    }

    return(0);
}

static int cmd_status(void)
{
    uint8_t reply[PKT_LEN];

    if(simple_command(CMD_GET_DONGLE_STAT, 0, reply) != 0)
    {
        printf("  no reply to GetDongleStatus\n");
        return(-1);
    }

    dump("status reply", reply, PKT_LEN);
    printf("  keyboard connected: %s\n", reply[4] > 0 ? "yes" : "no");

    return(0);
}

static int cmd_password(void)
{
    uint8_t reply[PKT_LEN];

    if(simple_command(CMD_GET_PASSWORD, 0, reply) != 0)
    {
        printf("  no reply to GetPassword\n");
        return(-1);
    }

    uint32_t pwd = (uint32_t)reply[4] | ((uint32_t)reply[5] << 8) |
                   ((uint32_t)reply[6] << 16) | ((uint32_t)reply[7] << 24);
    uint32_t low = (uint32_t)reply[8] | ((uint32_t)reply[9] << 8);

    dump("password reply", reply, PKT_LEN);
    printf("  pwd=0x%08X+0x%04X  fw version=%02X%02X\n", pwd, low, reply[12], reply[13]);

    return(0);
}

static int cmd_colors(uint8_t board, const char* save_to)
{
    uint8_t buf[COLOR_LEN];
    int     have = read_colors(buf, board);

    printf("  got %d/%d colour bytes\n", have, COLOR_LEN);

    if(have < COLOR_LEN)
    {
        return(-1);
    }

    print_colors(buf, 1);

    if(save_to != NULL)
    {
        FILE* f = fopen(save_to, "wb");

        if(f == NULL)
        {
            fprintf(stderr, "cannot write %s: %s\n", save_to, strerror(errno));
            return(-1);
        }

        fwrite(buf, 1, COLOR_LEN, f);
        fclose(f);
        printf("  saved to %s\n", save_to);
    }

    return(0);
}

static int cmd_load(const char* file)
{
    uint8_t buf[COLOR_LEN];
    FILE*   f = fopen(file, "rb");

    if(f == NULL)
    {
        fprintf(stderr, "cannot read %s: %s\n", file, strerror(errno));
        return(-1);
    }

    size_t got = fread(buf, 1, COLOR_LEN, f);
    fclose(f);

    if(got != COLOR_LEN)
    {
        fprintf(stderr, "%s is %zu bytes, expected %d\n", file, got, COLOR_LEN);
        return(-1);
    }

    printf("  restoring %d bytes from %s\n", COLOR_LEN, file);
    return(write_colors(buf, 0));
}

static int cmd_fill(uint32_t rgb)
{
    uint8_t colors[COLOR_LEN];
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    for(int i = 0; i < LED_COUNT; i++)
    {
        colors[i]                = r;
        colors[LED_COUNT + i]    = g;
        colors[LED_COUNT * 2 + i] = b;
    }

    printf("  filling %d LEDs with #%02X%02X%02X\n", LED_COUNT, r, g, b);
    return(write_colors(colors, 0));
}

static void usage(const char* argv0)
{
    printf("usage: %s <command> [args] [-d /dev/hidrawN]\n\n", argv0);
    printf("  info                device info (no traffic)\n");
    printf("  status              GetDongleStatus (7)\n");
    printf("  password            GetPassword (5)\n");
    printf("  colors [board]      GetLedColors (66), board defaults to 0\n");
    printf("  save <file>         dump the colour buffer\n");
    printf("  load <file>         write a colour buffer back\n");
    printf("  fill <RRGGBB>       set every LED (2, chunked + acked)\n");
}

int main(int argc, char** argv)
{
    const char* path    = "/dev/hidraw1";
    const char* cmd     = (argc > 1) ? argv[1] : NULL;
    const char* arg     = (argc > 2 && argv[2][0] != '-') ? argv[2] : NULL;
    int         board   = 0;
    int         result  = 1;

    if(cmd == NULL)
    {
        usage(argv[0]);
        return(1);
    }

    for(int i = 2; i < argc; i++)
    {
        if(strcmp(argv[i], "-d") == 0 && i + 1 < argc)
        {
            path = argv[++i];
        }
    }

    if(arg != NULL && (strcmp(cmd, "colors") == 0))
    {
        board = (int)strtol(arg, NULL, 0);
    }

    g_fd = open(path, O_RDWR);

    if(g_fd < 0)
    {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return(1);
    }

    printf("== %s ==\n", path);

    if(strcmp(cmd, "info") == 0)
    {
        result = cmd_info();
    }
    else if(strcmp(cmd, "status") == 0)
    {
        result = cmd_status();
    }
    else if(strcmp(cmd, "password") == 0)
    {
        result = cmd_password();
    }
    else if(strcmp(cmd, "colors") == 0)
    {
        result = cmd_colors((uint8_t)board, NULL);
    }
    else if(strcmp(cmd, "save") == 0 && arg != NULL)
    {
        result = cmd_colors(0, arg);
    }
    else if(strcmp(cmd, "load") == 0 && arg != NULL)
    {
        result = cmd_load(arg);
    }
    else if(strcmp(cmd, "fill") == 0 && arg != NULL)
    {
        result = cmd_fill((uint32_t)strtoul(arg, NULL, 16));
    }
    else
    {
        usage(argv[0]);
    }

    close(g_fd);
    return(result == 0 ? 0 : 1);
}
