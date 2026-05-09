#include <zephyr/kernel.h>
#include "colibri/leds.h"
#include "colibri/slots.h"
#include "wasm3.h"
#include "m3_env.h"

static int ok_color = 0x000400;
static int error_color = 0x080000;
static int warning_color = 0x040400;
static int info_color = 0x010108;

static m3ApiRawFunction(set_rgb_color)
{
    m3ApiGetArg(int, rgb)
    int slot = slot_selected();
    if (rgb >= 0)
    {
        rgb_set_color(slot, 0);
    }
    else
    {
        switch (rgb)
        {
        case -1: // Ok
            rgb_set_color(slot, ok_color);
            break;
        case -2: // Warning
            rgb_set_color(slot, warning_color);
            break;
        case -3: // Error
            rgb_set_color(slot, error_color);
            break;
        case -4: // Info
            rgb_set_color(slot, info_color);
            break;
        default:
            break;
        }
    }
    return 0;
}

static m3ApiRawFunction(set_rgb_off)
{
    rgb_set_color(slot_selected(), 0);
    return 0;
}

void wasm_rgb_api_init(IM3Module module)
{
    m3_LinkRawFunction(module, "*", "set_rgb_color", "v(i)", set_rgb_color);
    m3_LinkRawFunction(module, "*", "set_rgb_off", "v()", set_rgb_off);
}
