#ifndef COLIBRI_IO_H
#define COLIBRI_IO_H
#include <stdint.h>
#include <stddef.h>

// Functions the HOST provides to the DRIVER (Imports)
typedef struct {
    long (* get)(uint32_t event);

    void (*publish)(uint32_t event, uint64_t value);

    void (*subscribe)(uint16_t event_type);

    void (*unsubscribe)(uint16_t event_type);

    void (*i2c_write)(uint8_t address, uint8_t* data, uint16_t length);

    void (*i2c_read)(uint8_t address, uint8_t* data, uint16_t length);

    void (*spi_write)(uint8_t address, uint8_t* data, uint16_t length);

    void (*spi_read)(uint8_t address, uint8_t* to_write, uint8_t* to_read, uint16_t length);

    void (*set_rgb_color)(int32_t color);
} Host_API_t;

// A helper type for driver functions that take arguments
typedef void (*driver_event_fn)(int32_t, uint64_t);
typedef void (*driver_init_fn)();
typedef void (*driver_loaded_fn)();
typedef void (*driver_unloading_fn)();


// Functions the DRIVER provides to the HOST (Exports)
typedef struct {
    // Called once after power-up, immediately after the wasm has been loaded into
    // RAM, before calling loaded();
    // Typically the implementation should subscribe to a timer event, otherwise
    // it will only be called on power-up. For instance;
    //     subscribe(TIME_100_MS);
    void (*init)(void);

    // Called each time the wasm binary is loaded into RAM. Unloading may happen
    // when there are no subscriptions, that happens frequently. For instance,
    // if the init() function only calls;
    //     subscribe(TIME_1_HOUR);
    // then the wasm binary might be unloaded after its execution and reloaded
    // just before it is time to call the event() function.
    void (*loaded)(void);

    // A value was published from an id previously subscribed to.
    void (*event)(int32_t event, uint64_t value);

} Driver_Interface_t;

#endif
