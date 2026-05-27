#include <stdlib.h>
#include <zephyr/kernel.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "colibri-sdk/colibri.h"
#include "colibri-sdk/colibri-events.h"
#include "colibri/luaInterface.h"

static lua_State *lua_state;

void lua_event(event_t event, int64_t value)
{
    lua_getglobal(lua_state, "event");

    if (!lua_isfunction(lua_state, -1)) {
        printk("Error: 'event' is not a defined Lua function\n");
        lua_pop(lua_state, 1);
        return;
    }

    lua_pushinteger(lua_state, event.value);
    lua_pushinteger(lua_state, value);

    if (lua_pcall(lua_state, 2, 0, 0) != LUA_OK) {
        printk("Lua Error: %s\n", lua_tostring(lua_state, -1));
        lua_pop(lua_state, 1);
        return;
    }

    lua_gc(lua_state, LUA_GCCOLLECT, 0);
}

int lua_publish(lua_State *L)
{
    int32_t event = (int32_t) luaL_checknumber(L, 1);
    int64_t value = (int64_t) luaL_checknumber(L, 2);
    event_t ev;
    ev.value = event;
    events_publish(ev, value);
    return 1;
}

int lua_event_get(lua_State *L)
{
    int32_t event = (int32_t) luaL_checknumber(L, 1);
    event_t ev;
    ev.value = event;
    int64_t value = events_get(ev);
    // TODO/NOTE/WARNING: lua_Number is a floating point type, which may not be suitable for representing large integer values. We need to be very aware of this. Update documentation.
    lua_pushnumber(L, (lua_Number)value);
    return 1;
}


int lua_subscribe(lua_State *L)
{
    int32_t event = (int32_t) luaL_checknumber(L, 1);
    uint32_t index = (uint32_t) events_subscribe(event, lua_event);
    lua_pushinteger(L, index);
    return 1;
}

int lua_unsubscribe(lua_State *L)
{
    int32_t index = (int32_t) luaL_checknumber(L, 1);
    events_unsubscribe((void *)index);
    return 1;
}

static const luaL_Reg system_api[] = {
    {"publish", lua_publish},
    {"subscribe", lua_subscribe},
    {"unsubscribe", lua_unsubscribe},
    {"event_value", lua_event_get},
    {NULL, NULL}
};

int lua_initialize()
{
    lua_state = luaL_newstate();
    if (lua_state == NULL) {
        printk("lua: cannot create state: not enough memory\n");
        return -ENOMEM;
    }
    luaL_openlibs(lua_state);
    lua_register_event_constants(lua_state);
    lua_install_uc_globals(lua_state);

    int error = lua_load_script(lua_state, "");
    if (error != 0) {
        printk("lua: unable to load script\n");
        /* Script not loaded, nothing will be subscribed to. */
        return error;
    }
    return 0;
}


//This function is called by Lua if it cannot handle an occured error.
void luaAbort()
{
    lua_writestringerror("luaAbort", sizeof("luaAbort"));
    while (1)
    {
    }
}

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