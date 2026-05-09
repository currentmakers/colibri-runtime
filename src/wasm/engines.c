#include <zephyr/kernel.h>

#include "colibri/events.h"
#include "colibri/slots.h"
#include "colibri/wasm.h"

#include "mock/io_aic.h"
#include "mock/io_aiv.h"
#include "mock/io_aqv.h"
#include "mock/io_pid1.h"
#include "mock/io_ssr.h"
#include "mock/io_dio1.h"
#include "mock/io_empty.h"
#include "mock/rgb_blink.h"

static wasm_engine io_engines[8];
static wasm_engine user_engines[1] = {0};
static IM3Environment env;

static const unsigned int code_size[] = {
    io_empty_len,
    io_aiv_len,
    io_aic_len,
    io_aqv_len,
    io_pid1_len,
    io_ssr_len,
    io_dio1_len,
    io_empty_len
};

static const uint8_t *code[] = {
    io_empty,
    io_aiv,
    io_aic,
    io_aqv,
    io_pid1,
    io_ssr,
    io_dio1,
    io_empty
};

void setup_memory(wasm_engine *engine)
{
    engine->wasm_memory.runtime = engine->runtime;
    engine->wasm_memory.length = 4096;
    engine->wasm_memory.maxStack = (m3slot_t*)engine->wasm_memory.runtime->stack + engine->wasm_memory.runtime->numStackSlots;
    engine->wasm_memory.runtime->memory.mallocated = &engine->wasm_memory;
}

M3Result create_runtime(wasm_engine *engine, const uint8_t *wasm_code, const size_t codesize)
{
    M3Result result;

    engine->runtime = m3_NewRuntime(env, 1024, NULL);
    if (!engine->runtime) printk("NewRuntime: %s\n", "failed");
    setup_memory(engine);

    result = m3_ParseModule(env, &engine->module, wasm_code, codesize);
    if (result) printk("ParseModule: %s\n", result);

    result = m3_LoadModule(engine->runtime, engine->module);
    if (result) printk("LoadModule: %s\n", result);

    wasm_rgb_api_init(engine->module);

    result = m3_FindFunction(&engine->fn_event, engine->runtime, "event");
    if (result) printk("FindFunction event: %s\n", result);
    result = m3_FindFunction(&engine->fn_init, engine->runtime, "init");
    if (result) printk("FindFunction init: %s\n", result);
    result = m3_FindFunction(&engine->fn_loaded, engine->runtime, "loaded");
    if (result) printk("FindFunction loaded: %s\n", result);
    result = m3_FindFunction(&engine->fn_unloading, engine->runtime, "unloading");
    if (result) printk("FindFunction unloading: %s\n", result);

    result = m3_CallV(engine->fn_init, 0);
    if (result) printk("init(): %s\n", result);
    return result;
}

void wasm_initialize(int slots)
{
    M3Result result;

    if (!env)
    {
        env = m3_NewEnvironment();
        if (!env) printk("NewEnvironment: %s\n", "failed");
    }
    for (int i = 0; i <= slots; i++)
    {
        printk("Initializing wasm engine for slot %d\n", i);
        m3_FreeRuntime(io_engines[i].runtime);
        result = create_runtime(&io_engines[i], code[i], code_size[i]);
        if (result) printk("create_runtime: %s\n", result);
    }
}

void wasm_user_tick(uint64_t now)
{
    uint32_t event = create_user_event(COLIBRI_EVENT_TYPE_TIME, now);
    wasm_engine user_engine = user_engines[0];
    if ( user_engine.runtime != NULL)
    {
        M3Result result = m3_CallV(user_engine.fn_event, event, now);
        if (result) printk("event(%d, %lld): %s\n", event, now, result);
    }
}

void wasm_io_tick(uint64_t now, int slot)
{
    uint32_t event = create_io_event(slot, COLIBRI_EVENT_TYPE_TIME, 0);
    M3Result result = m3_CallV(io_engines[slot].fn_event, event, now);
    if (result) printk("event(%d, %lld) -> %d: %s\n", event, now, slot, result);
}