#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h"
#include "colibri-sdk/colibri.h"
#include "colibri/luaInterface.h"
#include "colibri/slots.h"

#define LUA_STACK_SIZE   (32 * 1024)   /* 32 KiB, placed in CCM */
#define LUA_PRIORITY     10
#define LUA_TICK_PERIOD  K_MSEC(100)

/*
 * Lua thread stack in CCM SRAM (0x10000000 on STM32F405).
 *
 *  - __dtcm_noinit_section: skip boot-time zeroing; Zephyr initializes
 *    the active part of the stack itself when the thread starts.
 *  - The macro yields an array that satisfies Zephyr's stack-alignment
 *    requirements for the architecture.
 *
 * Note: K_THREAD_STACK_DEFINE does not accept a section attribute, so we
 * declare the array manually using the same underlying primitives.
 */
__dtcm_noinit_section
static K_KERNEL_STACK_MEMBER(lua_thread_stack, LUA_STACK_SIZE);

static struct k_thread  lua_thread_data;
static k_tid_t          lua_thread_tid;

static lua_State *lua_state;

/* ------------------------------------------------------------------------- */
/* One tick of the Lua event loop. Called from the Lua thread context only.  */
/* ------------------------------------------------------------------------- */
static void lua_tick(void)
{
    lua_getglobal(lua_state, "event");

    if (!lua_isfunction(lua_state, -1)) {
        printk("Error: 'event' is not a defined Lua function\n");
        lua_pop(lua_state, 1);
        return;
    }

    lua_Number event_id = create_user_event(COLIBRI_EVENT_TYPE_TIME_PERIOD, 0);
    lua_pushnumber(lua_state, event_id);

    lua_Number now = (lua_Number) k_uptime_get();
    lua_pushnumber(lua_state, now);

    if (lua_pcall(lua_state, 2, 0, 0) != LUA_OK) {
        printk("Lua Error: %s\n", lua_tostring(lua_state, -1));
        lua_pop(lua_state, 1);
        return;
    }

    lua_gc(lua_state, LUA_GCCOLLECT, 0);
}

/* ------------------------------------------------------------------------- */
/* Thread entry point: initialize the VM, load the script, then tick forever.*/
/* ------------------------------------------------------------------------- */
static void lua_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    lua_state = luaL_newstate();
    if (lua_state == NULL) {
        printk("lua: cannot create state: not enough memory\n");
        return;
    }
    luaL_openlibs(lua_state);
    lua_register_event_constants(lua_state);
    lua_install_uc_globals(lua_state);

    if (lua_load_script(lua_state, "") != 0) {
        printk("lua: unable to load script\n");
        /* Continue ticking anyway; 'event' lookup will just fail cleanly. */
    }

    for (;;) {
        lua_tick();
        k_sleep(LUA_TICK_PERIOD);
    }
}

/* ------------------------------------------------------------------------- */
/* Public init: spawn the Lua thread. Returns 0 on success.                  */
/* ------------------------------------------------------------------------- */
int lua_initialize(void)
{
    lua_thread_tid = k_thread_create(
        &lua_thread_data,
        lua_thread_stack,
        K_KERNEL_STACK_SIZEOF(lua_thread_stack),
        lua_thread_entry,
        NULL, NULL, NULL,
        LUA_PRIORITY,
        0,
        K_NO_WAIT);

    if (lua_thread_tid == NULL) {
        printk("lua: failed to create thread\n");
        return -ENOMEM;
    }

    k_thread_name_set(lua_thread_tid, "lua");
    return 0;
}