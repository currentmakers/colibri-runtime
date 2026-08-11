#ifndef COLIBRI_RUNTIME_HOST_API_H
#define COLIBRI_RUNTIME_HOST_API_H
#include <stdint.h>
#include "colibri-sdk/colibri-io.h"

void hostapi_call_driver_with_r9(void* func_ptr, void* r9_target_ram,
    uint32_t arg1, uint32_t arg2);

uint32_t slot_call_driver_with_r9_ret(void* func_ptr, void* r9_target_ram,
    uint32_t arg1, uint32_t arg2);

void slot_call_driver_with_r9_4(void* func_ptr, void* r9_target_ram,
    uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif //COLIBRI_RUNTIME_HOST_API_H
