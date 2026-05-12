#include <zephyr/kernel.h>
#include "colibri/luaInterface.h"
#include "colibri/leds.h"
#include "colibri/slots.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvm.h"

static int ok_color = 0x000400;
static int error_color = 0x080000;
static int warning_color = 0x040400;
static int info_color = 0x010108;


//This function is called by Lua if it cannot handle an occured error.
void luaAbort()
{
    lua_writestringerror("luaAbort", sizeof("luaAbort"));
    while (1)
    {
    }
}

static int set_rgb_color(lua_State* L)
{
    int32_t rgb = (int32_t) luaL_checknumber(L, 1);

    if (rgb >= 0)
    {
        rgb_set_color(0, rgb);
    }
    else
    {
        switch (rgb)
        {
        case -1: // Ok
            rgb_set_color(0, ok_color);
            break;
        case -2: // Warning
            rgb_set_color(0, warning_color);
            break;
        case -3: // Error
            rgb_set_color(0, error_color);
            break;
        case -4: // Info
            rgb_set_color(0, info_color);
            break;
        default:
            break;
        }
    }
    return 1;
}

static const luaL_Reg system_api[] = {
    {"set_rgb", set_rgb_color},
    {"publish", set_rgb_color},
    {"subscribe", set_rgb_color},
    {"unsubscribe", set_rgb_color},
    {NULL, NULL}
};

void lua_install_uc_globals(lua_State *L)
{
    for (const luaL_Reg *r = system_api; r->name != NULL; r++) {
        lua_pushcfunction(L, r->func);
        lua_setglobal(L, r->name);
    }
}

LUAMOD_API int luaopen_uc(lua_State* L)
{
    luaL_newlib(L, system_api);
    return 1;
}

void lua_register_event_constants(lua_State *L)
{
    lua_newtable(L);

    lua_pushstring(L, "UNKNOWN");
    lua_pushinteger(L, 0x0000);
    lua_settable(L, -3);

    lua_pushstring(L, "TIME_PERIOD");
    lua_pushinteger(L, 0x0100);
    lua_settable(L, -3);

    lua_pushstring(L, "TIME");
    lua_pushinteger(L, 0x0200);
    lua_settable(L, -3);

    lua_pushstring(L, "COUNTER");
    lua_pushinteger(L, 0x0300);
    lua_settable(L, -3);

    lua_pushstring(L, "ERROR_CODE");
    lua_pushinteger(L, 0x0400);
    lua_settable(L, -3);

    lua_pushstring(L, "MEASURED_VALUE");
    lua_pushinteger(L, 0x0500);
    lua_settable(L, -3);

    lua_pushstring(L, "COMPUTED_VALUE");
    lua_pushinteger(L, 0x0600);
    lua_settable(L, -3);

    lua_pushstring(L, "SETPOINT");
    lua_pushinteger(L, 0x0700);
    lua_settable(L, -3);

    lua_pushstring(L, "MIN_VALUE");
    lua_pushinteger(L, 0x0800);
    lua_settable(L, -3);

    lua_pushstring(L, "MAX_VALUE");
    lua_pushinteger(L, 0x0900);
    lua_settable(L, -3);

    lua_pushstring(L, "LOW_THRESHOLD");
    lua_pushinteger(L, 0x0A00);
    lua_settable(L, -3);

    lua_pushstring(L, "HIGH_THRESHOLD");
    lua_pushinteger(L, 0x0B00);
    lua_settable(L, -3);

    lua_pushstring(L, "RUN_INDICATION");
    lua_pushinteger(L, 0x0C00);
    lua_settable(L, -3);

    lua_pushstring(L, "ALARM_INDICATION");
    lua_pushinteger(L, 0x0D00);
    lua_settable(L, -3);

    lua_pushstring(L, "RGB_SET");
    lua_pushinteger(L, 0x0E00);
    lua_settable(L, -3);

    // 3. Name the table globally as "HW" and pop it off the stack
    lua_setglobal(L, "events");
}