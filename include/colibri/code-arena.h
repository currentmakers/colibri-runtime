#ifndef COLIBRI_RUNTIME_CODE_ARENA_H
#define COLIBRI_RUNTIME_CODE_ARENA_H

#include <stdint.h>

#include <zephyr/devicetree.h>

#include "colibri/slots.h"

/*
 * Executable RAM arena holding one Position Independent Code window per slot.
 *
 * The arena is carved out of sram0 in app.overlay rather than declared as an
 * ordinary array, for two reasons:
 *
 *   - Zephyr's SRAM MPU region carries the XN (execute-never) bit whenever
 *     CONFIG_XIP=y, so branching into a normal .bss buffer raises an
 *     instruction access violation. src/slots/code-arena.c gives the arena its
 *     own MPU regions with XN clear, and because the ARMv7-M MPU resolves
 *     overlapping regions in favour of the highest-numbered one, those regions
 *     override the SRAM default.
 *
 *   - It pins the arena to a fixed, MPU-alignable address. An __aligned(16384)
 *     array would drift with the rest of .bss and take the region table with it.
 *
 * The arena is (NOLOAD): unlike .bss it is *not* zeroed at boot and retains
 * whatever survived the last reset. slots_initialize() clears it explicitly.
 */
#define SLOT_CODE_ARENA_NODE DT_NODELABEL(slot_code_arena)
#define SLOT_CODE_ARENA_BASE DT_REG_ADDR(SLOT_CODE_ARENA_NODE)
#define SLOT_CODE_ARENA_SIZE DT_REG_SIZE(SLOT_CODE_ARENA_NODE)

/*
 * Per-slot code window. Must be large enough for eeprom_layout_t.pic_arm;
 * code-arena.c asserts that at build time.
 */
#define SLOT_CODE_SIZE (SLOT_CODE_ARENA_SIZE / MAX_SLOTS)

extern uint8_t slot_code[MAX_SLOTS][SLOT_CODE_SIZE];

#endif
