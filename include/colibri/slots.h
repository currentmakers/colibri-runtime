#ifndef COLIBRI_RUNTIME_SLOTS_H
#define COLIBRI_RUNTIME_SLOTS_H

#include <stdbool.h>
#include <stdint.h>
#include <colibri-sdk/colibri-io.h>
#define MAX_SLOTS 8

struct device;   /* Zephyr device handle, forward-declared for the EEPROM API */

typedef struct
{
    uint32_t serial_number;
    char* vendor;
    char* model;
    char revision[8];
    char* doc_link;
    char* product_link;
    uint8_t driver_ram[256] __attribute__((aligned(8)));    // Dedicated RAM for this slot's static variables (.data/.bss)
    const Driver_Interface_t* vmt;                          // Pointer to the driver's functions (returned by register)
    uint8_t code_pic[2048] __attribute__((aligned(8)));
} slot_info_t;

int slots_initialize();
void slots_tick(uint8_t slot, int64_t now);

/*
 * Borrow/return a slot's EEPROM device under the shared bus lock. acquire()
 * blocks until the I/O thread is between ticks, selects the slot and returns
 * its EEPROM device (NULL if the slot is invalid or not ready). Always pair a
 * non-NULL acquire() with release().
 */
const struct device* slot_acquire(uint8_t slot);
void slot_release(void);

int slot_set_power_state(uint8_t slot, bool enabled);
int slot_set_reset_state(uint8_t slot, bool asserted);

void slot_count_initialize();
int slot_count();

void slot_select(uint8_t slot);
int slot_selected();

/* Defined in src/slots/i2c.c */
void slot_i2c_write(uint8_t address, uint8_t *data, uint16_t length);
void slot_i2c_read(uint8_t address, uint8_t *data, uint16_t length);

/* Defined in src/slots/spi.c */
void slot_spi_write(uint8_t address, uint8_t *data, uint16_t length);
void slot_spi_read(uint8_t address, uint8_t *to_write, uint8_t *to_read, uint16_t length);

extern const Host_API_t host_api;


#endif
