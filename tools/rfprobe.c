/*---------------------------------------------------------*\
| rfprobe.c                                                 |
|                                                           |
|   READ-ONLY probe of the R87 Pro 2.4G receiver (3554:fa09)|
|   hidraw nodes: prints the device info, reads every feature|
|   report the descriptor declares, and dumps incoming input |
|   reports for a few seconds.                              |
|                                                           |
|   It never writes to the device.                          |
|                                                           |
|   Build: cc -O2 -o rfprobe rfprobe.c                      |
\*---------------------------------------------------------*/

#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(const char* tag, const unsigned char* data, int len)
{
    printf("  %-22s (%2d):", tag, len);

    for(int i = 0; i < len; i++)
    {
        printf(" %02X", data[i]);
    }

    printf("\n");
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "/dev/hidraw1";
    int         fd   = open(path, O_RDWR | O_NONBLOCK);

    if(fd < 0)
    {
        printf("cannot open %s: %s\n", path, strerror(errno));
        return(1);
    }

    printf("== %s ==\n", path);

    struct hidraw_devinfo info;
    char                  name[128] = { 0 };

    if(ioctl(fd, HIDIOCGRAWINFO, &info) == 0)
    {
        printf("  bustype=%04X vendor=%04X product=%04X\n",
               info.bustype, info.vendor, info.product);
    }

    if(ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name) >= 0)
    {
        printf("  name=%s\n", name);
    }

    printf("== feature reports (read only) ==\n");

    for(int id = 0; id <= 0x20; id++)
    {
        unsigned char buf[128];

        memset(buf, 0, sizeof(buf));
        buf[0] = (unsigned char)id;

        int r = ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);

        if(r > 0)
        {
            char tag[32];
            snprintf(tag, sizeof(tag), "feature id %02X", id);
            dump(tag, buf, r);
        }
    }

    printf("== input reports for 3 s (press keys / move nothing) ==\n");

    for(int i = 0; i < 30; i++)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);

        struct timeval tv = { 0, 100000 };

        if(select(fd + 1, &set, NULL, NULL, &tv) > 0)
        {
            unsigned char buf[128];
            int           r = read(fd, buf, sizeof(buf));

            if(r > 0)
            {
                dump("input", buf, r);
            }
        }
    }

    close(fd);
    return(0);
}
