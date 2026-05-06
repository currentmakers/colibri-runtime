#ifndef COLIBRI_RUNTIME_SLOTS_H
#define COLIBRI_RUNTIME_SLOTS_H

#include <stdbool.h>
#include <stdint.h>

int slots_init();
int slot_set_power_state(uint8_t slot, bool enabled);
int slot_set_reset_state(uint8_t slot, bool asserted);

void slot_count_init();
int slot_count();

void slot_select(int slot);
int slot_selected();

#endif
