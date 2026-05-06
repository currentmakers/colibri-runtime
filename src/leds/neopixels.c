#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include "colibri/leds.h"

#define LED_STRIP_LABEL DT_NODE_FULL_NAME(LED_STRIP_NODE)
#define LED_STRIP_NODE DT_ALIAS(led_strip)
#define LED_STRIP_COUNT DT_PROP(LED_STRIP_NODE, chain_length)

static const struct device* const strip = DEVICE_DT_GET(LED_STRIP_NODE);
static struct led_rgb pixels[LED_STRIP_COUNT];

void rgb_set_color(int slot, int rgb)
{
    pixels[slot].r = rgb >> 16 & 0xFF;
    pixels[slot].g = rgb >> 8 & 0xFF;
    pixels[slot].b = rgb& 0xFF;
    led_strip_update_rgb(strip, pixels, LED_STRIP_COUNT);
}

void rgb_set_rgb(int slot, int r, int g, int b)
{
    pixels[slot].r = r;
    pixels[slot].g = g;
    pixels[slot].b = b;
    led_strip_update_rgb(strip, pixels, LED_STRIP_COUNT);
}

void rgb_set_off(int slot)
{
    rgb_set_rgb(slot, 0,0,0);
}

void rgb_set_red(int slot)
{
    rgb_set_rgb(slot, 0xff,0,0);
}

void rgb_set_green(int slot)
{
    rgb_set_rgb(slot, 0,0xff,0);
}

void rgb_set_blue(int slot)
{
    rgb_set_rgb(slot, 0,0,0xff);
}

int rgb_init()
{
    if (!device_is_ready(strip))
    {
        printk("led_strip device is not ready\n");
        return -1;
    }
    printk("Shield led_strip node: %s (chain_length=%d)\n", LED_STRIP_LABEL, LED_STRIP_COUNT);
    return 0;
}

