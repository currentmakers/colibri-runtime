#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "colibri-sdk/colibri-events.h"
#include "colibri/boot.h"
#include "colibri/environment.h"
#include "colibri/luaInterface.h"
#include "colibri/strings.h"
#include "colibri/supervisor.h"

static lua_State* lua_state;

static volatile bool initialized = false;

static void lua_event(const event_t event, const int64_t value, int32_t user_data)
{
    supervisor_timing_start(SUPERVISOR_TIMING_LUA);
    lua_getglobal(lua_state, "event");

    if (!lua_isfunction(lua_state, -1))
    {
        printk("Error: 'event' is not a defined Lua function\n");
        lua_pop(lua_state, 1);
        return;
    }

    lua_pushinteger(lua_state, event.type);
    lua_pushinteger(lua_state, event.parameter);
    lua_pushinteger(lua_state, value);

    if (lua_pcall(lua_state, 3, 0, 0) != LUA_OK)
    {
        printk("Lua Error: %s\n", lua_tostring(lua_state, -1));
        lua_pop(lua_state, 1);
    }

    lua_gc(lua_state, LUA_GCCOLLECT, 0);
    supervisor_timing_stop(SUPERVISOR_TIMING_LUA);
}

int lua_publish(lua_State* L)
{
    int32_t event = (int32_t)luaL_checkinteger(L, 1); // event_type
    int32_t parameter = (int32_t)luaL_checkinteger(L, 2); // event parameter
    int64_t value = luaL_checkinteger(L, 3);
    event_t ev = {.slot = 0, .type = event, .io = false, .parameter = parameter};
    events_publish(ev, value);
    return 1;
}

int lua_event_get(lua_State* L)
{
    int32_t event = (int32_t)luaL_checknumber(L, 1);
    event_t ev;
    ev.value = event;
    int64_t value = events_get(ev);
    // TODO/NOTE/WARNING: lua_Number is a floating point type, which may not be suitable for representing large integer values. We need to be very aware of this. Update documentation.
    lua_pushnumber(L, (lua_Number)value);
    return 1;
}


int lua_subscribe(lua_State* L)
{
    int n = lua_gettop(L);
    if (n != 2)
        return luaL_error(L, "subscribe(event_type, parameter): expected 2 arguments, got %d", n);

    uint16_t event_type = ((uint16_t)luaL_checkinteger(L, 1)) & 0x03FF; // mask out 10 bits
    uint16_t event_parameter = (uint16_t)luaL_checkinteger(L, 2);
    int8_t slot = (int8_t)luaL_optinteger(L, 3, -1);
    event_t event;
    if (slot == -1)
        event = (event_t){.type = event_type, .parameter = event_parameter, .io = 0, .slot = 0};
    else
        event = (event_t){.type = event_type, .parameter = event_parameter, .io = 1, .slot = slot};
    int32_t index = (int32_t)events_subscribe(event, lua_event, 0);
    if (index == -1)
        return luaL_error(L, "subscribe(event_type, parameter) failed. Too many subscriptions");
    lua_pushinteger(L, index);
    return 1;
}

int lua_unsubscribe(lua_State* L)
{
    int n = lua_gettop(L);
    if (n != 1)
        return luaL_error(L, "unsubscribe(subscription): expected 1 argument, got %d", n);

    int32_t index = (int32_t)luaL_checkinteger(L, 1);
    events_unsubscribe((void*)index);
    return 0;
}

static const luaL_Reg system_api[] = {
    {"publish", lua_publish},
    {"subscribe", lua_subscribe},
    {"unsubscribe", lua_unsubscribe},
    {"event_value", lua_event_get},
    {NULL, NULL}
};


// This function is called by Lua if it cannot handle an error that occurred.

void luaAbort()
{
    lua_writestringerror("luaAbort", sizeof("luaAbort"));
    printk("luaAbort\n");
}

static void lua_install_uc_globals(lua_State* L)
{
    for (const luaL_Reg* r = system_api; r->name != NULL; r++)
    {
        lua_pushcfunction(L, r->func);
        lua_setglobal(L, r->name);
    }
}

LUAMOD_API int luaopen_uc(lua_State* L)
{
    luaL_newlib(L, system_api);
    return 1;
}

static void lua_register_event_constants(lua_State* L)
{
    lua_newtable(L);

    lua_pushstring(L, "UNKNOWN");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_UNKNOWN);
    lua_settable(L, -3);

    lua_pushstring(L, "TIME_PERIOD");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_TIME_PERIOD);
    lua_settable(L, -3);

    lua_pushstring(L, "TIME");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_TIME);
    lua_settable(L, -3);

    lua_pushstring(L, "OUTPUT");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_OUTPUT);
    lua_settable(L, -3);

    lua_pushstring(L, "COUNTER");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_COUNTER);
    lua_settable(L, -3);

    lua_pushstring(L, "ERROR_CODE");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_ERROR_CODE);
    lua_settable(L, -3);

    lua_pushstring(L, "MEASURED_VALUE");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_MEASURED_VALUE);
    lua_settable(L, -3);

    lua_pushstring(L, "COMPUTED_VALUE");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_COMPUTED_VALUE);
    lua_settable(L, -3);

    lua_pushstring(L, "SETPOINT");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_SETPOINT);
    lua_settable(L, -3);

    lua_pushstring(L, "MIN_VALUE");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_MIN_VALUE);
    lua_settable(L, -3);

    lua_pushstring(L, "MAX_VALUE");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_MAX_VALUE);
    lua_settable(L, -3);

    lua_pushstring(L, "LOW_THRESHOLD");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_LOW_THRESHOLD);
    lua_settable(L, -3);

    lua_pushstring(L, "HIGH_THRESHOLD");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_HIGH_THRESHOLD);
    lua_settable(L, -3);

    lua_pushstring(L, "RUN_INDICATION");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_RUN_INDICATION);
    lua_settable(L, -3);

    lua_pushstring(L, "ALARM_INDICATION");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_ALARM_INDICATION);
    lua_settable(L, -3);

    lua_pushstring(L, "RGB_SET");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_RGB_INDICATOR);
    lua_settable(L, -3);

    lua_pushstring(L, "MODBUS_UPDATE");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_MODBUS_UPDATE);
    lua_settable(L, -3);

    lua_pushstring(L, "MQTT");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_MQTT);
    lua_settable(L, -3);

    lua_pushstring(L, "CONFIG");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_CONFIG);
    lua_settable(L, -3);

    lua_pushstring(L, "ALL");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_ALL);
    lua_settable(L, -3);

    lua_pushstring(L, "NONE");
    lua_pushinteger(L, COLIBRI_EVENT_TYPE_NONE);
    lua_settable(L, -3);

    lua_setglobal(L, "events");
}

static void load_lua_file_if_present(char* file, size_t length)
{
    // char tmp[50];
    // strncpy(tmp, file, length);
    // tmp[length] = '\0';
    defer_alloc(f, 40)
    {
        char* filename = f;
        strncpy(filename, file, length);
        filename[length] = '\0';
        struct fs_dirent ent;
        int error = fs_stat(filename, &ent);
        if (error)
        {
            // file does not exist.
            printk("lua: script not present/found: %s\n", filename);
            return;
        }
        printk("lua: loading script %s\n", filename);
        error = lua_load_script(lua_state, filename);
        if (error != 0)
        {
            printk("lua: failed to load %s: %d\n", filename, error);
        }
    }
}

static void parse_lua_path(char* path_element, size_t length, void* lua_file)
{
    char tmp[50];
    strncpy(tmp, path_element, length);
    tmp[length] = '\0';
    printk("parse_lua_path: %d, %s\n", length, tmp);
    size_t size = length + strlen(path_element);
    defer_alloc(buffer, size + 2) // fit a possible '/' and a '\0'
    {
        char* filename = buffer; // cast buffer to char*
        strncpy(filename, path_element, length);
        filename[length] = '\0'; // end of path element copied
        filename[size] = '\0'; // end the full staring if '/' is at the end of path element
        filename[size + 1] = '\0'; // end the full staring if '/' is not at the end of path element
        char last_char = filename[strlen(filename) - 1];
        if (last_char != '/')
        {
            strcat(filename, "/");
            size++;
        }
        string_t* lua = lua_file;
        strncat(filename, lua->data, lua->size);
        load_lua_file_if_present(filename, strlen(filename));
    }
}

static void parse_lua_init(char* lua_filename, size_t length, void* user_data)
{
    char tmp[50];
    strncpy(tmp, lua_filename, length);
    tmp[length] = '\0';
    printk("parse_lua_init: %d, %s\n", length, tmp);
    if (lua_filename[0] == '/')
    {
        // Absolute path, don't parse LUA_PATH
        load_lua_file_if_present(lua_filename, length);
    }
    else
    {
        string_t lua_file;
        lua_file.data = lua_filename;
        lua_file.size = length;
        environment_path_read("LUA_PATH", parse_lua_path, &lua_file);
    }
}

static int lua_restart()
{
    initialized = false;
    lua_state = luaL_newstate();
    if (lua_state == NULL)
    {
        printk("lua: cannot create state: not enough memory\n");
        return -ENOMEM;
    }
    luaL_openlibs(lua_state);
    lua_register_event_constants(lua_state);
    lua_install_uc_globals(lua_state);
    environment_path_read("LUA_INIT", parse_lua_init, NULL);
    return 0;
}

static void lua_script_updated(event_t event, int64_t value, int32_t user_data)
{
    lua_close(lua_state);
    lua_restart();
}

int lua_initialize()
{
    events_subscribe((event_t){.type = COLIBRI_EVENT_TYPE_LUA_UPDATED, .parameter = 0, .io = 0, .slot = 0}, lua_script_updated, 0);
    return lua_restart();
}

bool lua_is_initialized()
{
    return initialized;
}
