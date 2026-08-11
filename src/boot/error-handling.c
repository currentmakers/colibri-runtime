#include <zephyr/kernel.h>
#include "colibri/boot.h"
#include "colibri/leds.h"

static void flash_red_led(int times)
{
    for (int i = 0; i < times; i++)
    {
        led_set_red();
        k_sleep(K_MSEC(200));
        led_set_off();
        k_sleep(K_MSEC(200));
    }
}

void boot_ok()
{
    for (int i = 0; i < 6; i++)
    {
        if (i % 2)
            led_set_green();
        else
            led_set_off();
        k_sleep(K_MSEC(100));
    }
    led_set_off();
}

void boot_error(int error)
{
    while (true)
    {
        switch (error)
        {
        case -ENODEV:
            flash_red_led(2);
            break;
        case -ENOENT:
        case -ENOTDIR:
            flash_red_led(3);
            break;
        case -ENOMEM:
            flash_red_led(4);
            break;
        case -EIO:
            flash_red_led(5);
            break;
        case -ENXIO:
            flash_red_led(6);
            break;
        default:
            flash_red_led(10);
            break;
        }
        led_set_red();
        k_sleep(K_MSEC(1000));
        led_set_off();
        k_sleep(K_MSEC(100));
    }
}
