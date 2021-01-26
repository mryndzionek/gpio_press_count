#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#include <sys/ioctl.h>
#include <linux/gpio.h>

#include <libdill.h>
#include "tests/assert.h"

#define SLEEP_BEFORE_EXIT_MS (1000UL)
#define COUNT_TIME_WINDOW_MS (1000UL)
#define DEBOUNCE_TIME_MS (50UL)

#define LOG(_level, _format, _args...)                  \
    fprintf(stdout, "[%010ld][" #_level "][%s] "_format \
                    "\n",                               \
            now(), __FUNCTION__, ##_args)

typedef struct
{
    bool state;
    uint32_t num;
} gpio_status_t;

typedef struct
{
    size_t count;
    uint32_t num;
} counter_status_t;

static void blink(uint16_t on_time, uint16_t off_time)
{
    int ret;
    char buf[16];

    if ((on_time == 0) || (off_time == 0))
    {
        int fd = open("/sys/class/leds/led1/trigger", O_WRONLY);
        assert(fd);
        write(fd, "none", sizeof("none"));
        close(fd);
    }
    else
    {

        int fd = open("/sys/class/leds/led1/trigger", O_WRONLY);
        assert(fd);
        write(fd, "timer", sizeof("timer"));
        close(fd);
        fd = open("/sys/class/leds/led1/delay_on", O_WRONLY);
        assert(fd);
        ret = snprintf(buf, sizeof(buf), "%u", on_time);
        assert(ret >= 0);
        assert(ret < sizeof(buf));
        write(fd, buf, ret);
        close(fd);
        fd = open("/sys/class/leds/led1/delay_off", O_WRONLY);
        assert(fd);
        ret = snprintf(buf, sizeof(buf), "%u", off_time);
        assert(ret >= 0);
        assert(ret < sizeof(buf));
        write(fd, buf, ret);
        close(fd);
    }
}

static coroutine void debouncer(int ch, uint32_t gpio_num, const char *name, uint32_t debounce_time_ms)
{
    int ret;
    bool steady = true;
    struct gpioevent_request req;

    LOG(INFO, "Starting debouncer (%d)", gpio_num);

    int fd = open("/dev/gpiochip0", 0);
    if (fd == -1)
    {
        ret = -errno;
        LOG(ERROR, "Failed to open /dev/gpiochip0");
        return;
    }
    else
    {
        req.lineoffset = gpio_num;
        req.handleflags = GPIOHANDLE_REQUEST_INPUT;
        req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
        strcpy(req.consumer_label, name);
        ret = ioctl(fd, GPIO_GET_LINEEVENT_IOCTL, &req);
        if (ret == -1)
        {
            ret = -errno;
            LOG(ERROR, "Failed to issue GET EVENT "
                       "IOCTL (%d)",
                ret);
        }
        else
        {
            ret = fcntl(req.fd, F_SETFL, O_NONBLOCK);
            if (ret != 0)
            {
                ret = -errno;
                LOG(ERROR, "Failed to issue F_SETFL "
                           "FCNTL (%d)",
                    ret);
            }
            else
            {
                struct gpiohandle_data data;

                LOG(INFO, "Entering debouncer loop");

                while (1)
                {
                    struct gpioevent_data event;

                    if (steady)
                    {
                        ret = fdin(req.fd, -1);
                    }
                    else
                    {
                        ret = fdin(req.fd, now() + debounce_time_ms);
                    }
                    if (ret == -1)
                    {
                        if (errno != ETIMEDOUT)
                        {
                            break;
                        }
                        ret = ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
                        assert(ret == 0);
                        gpio_status_t s = {.num = gpio_num, .state = data.values[0]};
                        ret = chsend(ch, &s, sizeof(s), -1);
                        errno_assert(ret == 0);
                        steady = true;
                    }
                    else
                    {
                        ret = read(req.fd, &event, sizeof(event));
                        if (ret == -1)
                        {
                            if (errno == -EAGAIN)
                            {
                                continue;
                            }
                            else
                            {
                                ret = -errno;
                                break;
                            }
                        }
                        assert(ret == sizeof(event));
                        steady = false;
                    }
                }
            }

            fdclean(req.fd);
        }
    }
    LOG(WARN, "Exiting");
    chdone(ch);
    close(fd);
}

static coroutine void counter(int in_ch, int out_ch, uint32_t gpio_num, const char *name, bool active, uint32_t time_window_ms)
{
    int ret;
    gpio_status_t gs;
    bool steady = true;
    counter_status_t cs = {.num = gpio_num, .count = 0};

    LOG(INFO, "Starting counter (%d)", gpio_num);

    while (true)
    {
        if (steady)
        {
            ret = chrecv(in_ch, &gs, sizeof(gs), -1);
            if (ret != 0)
            {
                break;
            }
            if (gs.state == active)
            {
                cs.count++;
                steady = false;
            }
        }
        else
        {
            ret = chrecv(in_ch, &gs, sizeof(gs), now() + time_window_ms);
            if (ret == -1)
            {
                assert(errno == ETIMEDOUT);
                ret = chsend(out_ch, &cs, sizeof(cs), -1);
                errno_assert(ret == 0);
                cs.count = 0;
                steady = true;
            }
            else
            {
                if (gs.state == active)
                {
                    cs.count++;
                }
            }
        }
    }
    LOG(WARN, "Exiting");
    chdone(out_ch);
}

int main(int argc, char *argv[])
{
    int ret, exit_code = 0;
    bool is_running = true;
    int chan1[2];
    int chan2[2];
    counter_status_t s;
    uint32_t gpio_num;
    int counts[argc - 2];

    if (argc < 3)
    {
        LOG(ERROR, "Please provide at least two arguments");
        exit(exit_code);
    }

    gpio_num = strtoul(argv[1], NULL, 0);
    if (gpio_num == 0)
    {
        LOG(ERROR, "Wrong GPIO number - %s", argv[1]);
        exit(exit_code);
    }

    for (int i = 0; i < argc - 2; i++)
    {
        ret = strtoul(argv[i + 2], NULL, 0);
        if (ret == 0)
        {
            LOG(ERROR, "Wrong count - %s", argv[i + 2]);
            exit(exit_code);
        }
        counts[i] = ret;
    }

    ret = chmake(chan1);
    errno_assert(ret == 0);
    assert(chan1[0] >= 0);
    assert(chan1[1] >= 0);

    ret = chmake(chan2);
    errno_assert(ret == 0);
    assert(chan2[0] >= 0);
    assert(chan2[1] >= 0);

    int h1 = go(debouncer(chan1[0], gpio_num, "powerbutton", DEBOUNCE_TIME_MS));
    errno_assert(h1 >= 0);

    int h2 = go(counter(chan1[1], chan2[0], gpio_num, "powerbutton", true, COUNT_TIME_WINDOW_MS));
    errno_assert(h2 >= 0);

    while (is_running)
    {
        int i;
        ret = chrecv(chan2[1], &s, sizeof(s), -1);
        if (ret != 0)
        {
            goto exit;
        }
        LOG(DEBUG, "GPIO: %d count: %lu", s.num, s.count);
        assert(s.num == gpio_num);

        LOG(DEBUG, "Detected count number: %lu", s.count);
        for (i = 0; i < argc - 2; i++)
        {
            if (s.count == counts[i])
            {
                is_running = false;
                break;
            }
        }
        if (is_running == false)
        {
            LOG(INFO, "Recognized count number: %lu", s.count);
            exit_code = s.count;

            //TODO make this LED signaling configurable
            switch (i)
            {
            case 0:
                blink(20, 20);
                break;

            case 1:
                blink(200, 200);
                break;

            default:
                blink(0, 0);
                break;
            }
            msleep(now() + SLEEP_BEFORE_EXIT_MS);
        }
    }

exit:
    LOG(WARN, "Exiting");
    ret = hclose(h1);
    errno_assert(ret == 0);

    ret = hclose(h2);
    errno_assert(ret == 0);

    ret = hclose(chan2[1]);
    errno_assert(ret == 0);
    ret = hclose(chan2[0]);
    errno_assert(ret == 0);

    ret = hclose(chan1[1]);
    errno_assert(ret == 0);
    ret = hclose(chan1[0]);
    errno_assert(ret == 0);

    exit(exit_code);
}