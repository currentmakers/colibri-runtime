#include <zephyr/kernel.h>
#include "colibri/leds.h"
#include "colibri/slots.h"
#include "wasm3.h"
#include "m3_env.h"

static int ok_color;
static int error_color;
static int warning_color;

static m3ApiRawFunction(set_rgb_ok_color)
{
    m3ApiGetArg(int, rgb)
    ok_color = rgb;
    return 0;
}

static m3ApiRawFunction(set_rgb_warning_color)
{
    m3ApiGetArg(int, rgb)
    warning_color = rgb;
    return 0;
}

static m3ApiRawFunction(set_rgb_error_color)
{
    m3ApiGetArg(int, rgb)
    error_color = rgb;
    return 0;
}

static m3ApiRawFunction(set_rgb_ok)
{
    rgb_set_color(slot_selected(), ok_color);
    return 0;
}

static m3ApiRawFunction(set_rgb_warning)
{
    rgb_set_color(slot_selected(), warning_color);
    return 0;
}

static m3ApiRawFunction(set_rgb_error)
{
    rgb_set_color(slot_selected(), error_color);
    return 0;
}

static m3ApiRawFunction(set_rgb_off)
{
    rgb_set_color(slot_selected(), 0);
    return 0;
}

void wasm_rgb_api_init(IM3Module module)
{
    m3_LinkRawFunction(module, "*", "set_rgb_ok", "v()", set_rgb_ok);
    m3_LinkRawFunction(module, "*", "set_rgb_warning", "v()", set_rgb_warning);
    m3_LinkRawFunction(module, "*", "set_rgb_error", "v()", set_rgb_error);
    m3_LinkRawFunction(module, "*", "set_rgb_off", "v()", set_rgb_off);
    m3_LinkRawFunction(module, "*", "set_rgb_ok_color", "v(i)", set_rgb_ok_color);
    m3_LinkRawFunction(module, "*", "set_rgb_warning_color", "v(i)", set_rgb_warning_color);
    m3_LinkRawFunction(module, "*", "set_rgb_error_color", "v(i)", set_rgb_error_color);
}

