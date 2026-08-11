#ifndef COLIBRI_RUNTIME_SUPERVISOR_H
#define COLIBRI_RUNTIME_SUPERVISOR_H
#include <stdint.h>

#define SUPERVISOR_TIMING_BASE 0
#define SUPERVISOR_TIMING_IO0 0     // Carrier board
#define SUPERVISOR_TIMING_IO1 1
#define SUPERVISOR_TIMING_IO2 2
#define SUPERVISOR_TIMING_IO3 3
#define SUPERVISOR_TIMING_IO4 4
#define SUPERVISOR_TIMING_IO5 5
#define SUPERVISOR_TIMING_IO6 6
#define SUPERVISOR_TIMING_IO7 7
#define SUPERVISOR_TIMING_LUA 8
#define SUPERVISOR_TIMING_END 9

#define SUPERVISOR_STACK_SIZE 1024
#define SUPERVISOR_PRIORITY 5

typedef struct
{
    const char *name;
    uint32_t start;
    volatile uint32_t cycles_sum;
    volatile uint16_t samples;
} timing_t;

void supervisor_timing_start(int index);
void supervisor_timing_stop(int index);

#endif //COLIBRI_RUNTIME_SUPERVISOR_H
