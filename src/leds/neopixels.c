#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include "colibri-sdk/colibri.h"
#include "colibri/leds.h"
#include "colibri/slots.h"

#define LED_STRIP_LABEL DT_NODE_FULL_NAME(LED_STRIP_NODE)
#define LED_STRIP_NODE DT_ALIAS(led_strip)
#define LED_STRIP_COUNT DT_PROP(LED_STRIP_NODE, chain_length)

static const struct device* const strip = DEVICE_DT_GET(LED_STRIP_NODE);
static struct led_rgb pixels[LED_STRIP_COUNT];

static volatile bool initialized;
static volatile bool changed;

static void rgb_set(const event_t event, const int64_t value, int32_t user_data)
{
    rgb_set_color(event.slot, (int32_t) value);
}

int rgb_initialize()
{
    for (int i = 0; i < slot_count(); i++)
    {
        // The receive of the events have not started yet, so instead of this.
        // events_subscribe((event_t) { create_user_event(COLIBRI_EVENT_TYPE_RGB_INDICATOR, i) }, rgb_set, 0);
        rgb_set_color(i, 0);
    }
    events_subscribe((event_t){.io = false, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 0}, rgb_set, 0);
    events_subscribe((event_t){.io = true, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 1}, rgb_set, 0);
    events_subscribe((event_t){.io = true, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 2}, rgb_set, 0);
    events_subscribe((event_t){.io = true, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 3}, rgb_set, 0);
    events_subscribe((event_t){.io = true, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 4}, rgb_set, 0);
    events_subscribe((event_t){.io = true, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 5}, rgb_set, 0);
    events_subscribe((event_t){.io = true, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 6}, rgb_set, 0);
    events_subscribe((event_t){.io = true, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .parameter = 0, .slot = 7}, rgb_set, 0);
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
    case COLIBRI_COLOR_OK:      // dark green
        return 0x002000;
    case COLIBRI_COLOR_WARNING: // orange
        return 0x200800;
    case COLIBRI_COLOR_INFO:    // dark blue
        return 0x000020;
    case COLIBRI_COLOR_ERROR:   // dark red
        return 0x040000;
    case -5:                    // yellow
        return 0x202000;
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
