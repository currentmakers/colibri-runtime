#ifndef COLIBRI_RUNTIME_SLOTS_H
#define COLIBRI_RUNTIME_SLOTS_H

#include <stdbool.h>
#include <stdint.h>
#define MAX_SLOTS 8

// Functions the HOST provides to the DRIVER (Imports)
typedef struct {
    long (* get)(int id);

    void (*publish)(int id, long value);

    void (*subscribe)(int id);

    void (*unsubscribe)(int id);

    // TODO: Remove set_rgb_color, use publish() instead.
    // If negative, a predefined color is used. If positive, the actual rgb value is used.
    void (*set_rgb_color)(int32_t color);

} Host_API_t;

// Functions the DRIVER provides to the HOST (Exports)
typedef struct {
// Mandatory functions
    // Called once after power-up, immediately after the code has been loaded into
    // RAM, before calling loaded();
    // Typically the implementation should subscribe to a timer event, otherwise
    // it will only be called on power-up. For instance;
    //     subscribe(TIME_100_MS);
    void (*init)(void);

    // Called each time the binary is loaded into RAM. Unloading may happen
    // when there are no subscriptions, that happens frequently. For instance,
    // if the init() function only calls;
    //     subscribe(TIME_1_HOUR);
    // then the binary might be unloaded after its execution and reloaded
    // just before it is time to call the event() function.
    void (*loaded)(void);

    // A value was published from an id previously subscribed to.
    void (*event)(int32_t id, uint64_t value);

    // Called by the framework just before the binary is unloaded from RAM.
    void (*unloading)(void);

// Optional SPI functions. MAY be implemented
    // None at the moment
} Driver_Interface_t;

// System API
#define COLIBRI_COLOR_OFF 0
#define COLIBRI_COLOR_OK (-1)
#define COLIBRI_COLOR_WARNING (-2)
#define COLIBRI_COLOR_ERROR (-3)
#define COLIBRI_COLOR_INFO (-4)

#define is_event_type(EVENT, TYPE) ((EVENT & 0xFF0000) == TYPE)
#define is_io_event(EVENT) ((EVENT & 0x1000000) == 0x1000000)
#define is_user_event(EVENT) ((EVENT & 0x1000000) == 0x0)
#define create_io_event(SLOT, EVENT_TYPE, EVENT_ID) (SLOT<<25 | 1<<24 | EVENT_TYPE<<16 | EVENT_ID)
#define create_user_event(EVENT_TYPE, EVENT_ID) (EVENT_TYPE<<16 | EVENT_ID)

#define COLIBRI_EVENT_TYPE_UNKNOWN            0x00
#define COLIBRI_EVENT_TYPE_TIME_PERIOD        0x01
#define COLIBRI_EVENT_TYPE_TIME               0x02
#define COLIBRI_EVENT_TYPE_COUNTER            0x03
#define COLIBRI_EVENT_TYPE_ERROR_CODE         0x04
#define COLIBRI_EVENT_TYPE_MEASURED_VALUE     0x05
#define COLIBRI_EVENT_TYPE_COMPUTED_VALUE     0x06
#define COLIBRI_EVENT_TYPE_SETPOINT           0x07
#define COLIBRI_EVENT_TYPE_MIN_VALUE          0x08
#define COLIBRI_EVENT_TYPE_MAX_VALUE          0x09
#define COLIBRI_EVENT_TYPE_LOW_THRESHOLD      0x0A
#define COLIBRI_EVENT_TYPE_HIGH_THRESHOLD     0x0B
#define COLIBRI_EVENT_TYPE_RUN_INDICATION     0x0C
#define COLIBRI_EVENT_TYPE_ALARM_INDICATION   0x0D

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
void slots_tick(uint64_t now, int slot);

int slot_set_power_state(uint8_t slot, bool enabled);
int slot_set_reset_state(uint8_t slot, bool asserted);

void slot_count_init();
int slot_count();

void slot_select(unsigned int slot);
int slot_selected();

extern const Host_API_t host_api;

#endif
