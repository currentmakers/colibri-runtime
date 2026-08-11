#ifndef COLIBRI_IO_H
#define COLIBRI_IO_H
#include <stdint.h>
#include "colibri-io.h"
#include "colibri-events.h"


// Functions the HOST provides to the DRIVER (Imports)
typedef struct {
    int64_t (* get)(event_t event);

    void (*publish)(event_t event, int64_t value);

    void* (*subscribe)(event_t event);

    void (*unsubscribe)(void*);

    int32_t (*i2c_write)(uint8_t address, uint8_t* data, uint16_t length);

    int32_t (*i2c_read)(uint8_t address, uint8_t* data, uint16_t length);

    int32_t (*spi_write)(uint8_t address, uint8_t* data, uint16_t length);

    int32_t (*spi_read)(uint8_t address, uint8_t* to_write, uint8_t* to_read, uint16_t length);
} Host_API_t;

// A helper type for driver functions that take arguments
typedef void (*driver_event_fn)(int32_t, int64_t);
typedef void (*driver_init_fn)();

// Functions the DRIVER provides to the HOST (Exports)
typedef struct {
    // Called once after power-up, immediately after the driver code has been loaded into RAM.
    // Typically, the implementation should subscribe to a timer event, otherwise
    // it will only be called on power-up. For instance;
    //     subscribe(TIME_100_MS);
    void (*initialize)(uint16_t slotnumber, uint8_t *calibration_data);

    // A value was published from an id previously subscribed to.
    void (*event)(event_t event, int64_t value);

} Driver_Interface_t;

#define SET_RGB(slot, color) { host->publish((event_t){.slot = (uint8_t)slot, .type = COLIBRI_EVENT_TYPE_RGB_INDICATOR, .io = true, .parameter = 0}, color); }

#endif
