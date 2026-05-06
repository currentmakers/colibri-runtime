#ifndef COLIBRI_RUNTIME_WASM_H
#define COLIBRI_RUNTIME_WASM_H

#include "m3_env.h"

typedef struct
{
    IM3Runtime runtime;
    M3MemoryHeader wasm_memory;
    IM3Module module;

    IM3Function fn_event;
    IM3Function fn_init;
    IM3Function fn_loaded;
    IM3Function fn_unloading;

} wasm_engine;

void wasm_rgb_api_init(IM3Module module);
void wasm_initialize(int slots);
void wasm_io_tick(uint64_t now, int slot);
void wasm_user_tick(uint64_t now);

#endif
