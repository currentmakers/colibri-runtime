#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include "colibri/leds.h"
#include "colibri/slots.h"

#define LED_STRIP_LABEL DT_NODE_FULL_NAME(LED_STRIP_NODE)
#define LED_STRIP_NODE DT_ALIAS(led_strip)
#define LED_STRIP_COUNT DT_PROP(LED_STRIP_NODE, chain_length)

static const struct device* const strip = DEVICE_DT_GET(LED_STRIP_NODE);
static struct led_rgb pixels[LED_STRIP_COUNT];

static volatile bool initialized;
static volatile bool changed;

static void rgb_set(event_t event, int64_t value)
{
    rgb_set_color(event.parameter, (uint32_t)value);
}

int rgb_initialize()
{
    for ( int i=0; i < slot_count(); i++ )
    {
        events_subscribe((event_t) { create_user_event(COLIBRI_EVENT_TYPE_RGB_INDICATOR, i) }, rgb_set);
    }
    return 0;
}

void rgb_update()
{
    if (!initialized)
    {
        if (!device_is_ready(strip))
        {
            printk("led_strip device is not ready\n");
            return;
        }
        printk("Shield led_strip node: %s (chain_length=%d)\n", LED_STRIP_LABEL, LED_STRIP_COUNT);
        initialized = true;
    }
    if (changed)
        led_strip_update_rgb(strip, pixels, LED_STRIP_COUNT);
    changed = false;
}

static int32_t translate(uint32_t rgb)
{
    switch (rgb)
    {
    case -1: // dark red
        return 0x040000;
    case -2: // dark green
        return 0x002000;
    case -3: // dark blue
        return 0x000020;
    case -4: // yellow
        return 0x202000;
    case -5: // orange
        return 0x200800;
    case -6:
        return 0x002020;
    case -7:
        return 0x200020;
    default:
        return 0;
    }
}

void rgb_set_color(int slot, int32_t rgb)
{
    if (rgb < 0)
        rgb = translate(rgb);
    pixels[slot].r = rgb >> 16 & 0xFF;
    pixels[slot].g = rgb >> 8 & 0xFF;
    pixels[slot].b = rgb & 0xFF;
    changed = true;
}

void rgb_set_rgb(int slot, int r, int g, int b)
{
    pixels[slot].r = r;
    pixels[slot].g = g;
    pixels[slot].b = b;
    changed = true;
}

void rgb_set_off(int slot)
{
    rgb_set_rgb(slot, 0, 0, 0);
}

void rgb_set_red(int slot)
{
    rgb_set_rgb(slot, 0x20, 0, 0);
}

void rgb_set_green(int slot)
{
    rgb_set_rgb(slot, 0, 0x20, 0);
}

void rgb_set_blue(int slot)
{
    rgb_set_rgb(slot, 0, 0, 0x20);
}
