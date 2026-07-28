#ifndef COLIBRI_RUNTIME_EVENTS_H
#define COLIBRI_RUNTIME_EVENTS_H

#define EVENT_MASK_TYPE 0x3FF0000
#define EVENT_MASK_PARAMETER 0xFFFF
#define EVENT_MASK_IO 0x04000000
#define EVENT_MASK_SLOT 0xF8000000

// 10 bits: Max 1024 EVENT_TYPEs
#define COLIBRI_EVENT_TYPE_NONE               0x00
#define COLIBRI_EVENT_TYPE_UNKNOWN            0x01
#define COLIBRI_EVENT_TYPE_ALL                0x02
#define COLIBRI_EVENT_TYPE_ERROR_CODE         0x03
#define COLIBRI_EVENT_TYPE_CONFIG             0x04
#define COLIBRI_EVENT_TYPE_TIME_PERIOD        0x05
#define COLIBRI_EVENT_TYPE_MEASURED_VALUE     0x06
#define COLIBRI_EVENT_TYPE_OUTPUT             0x07
#define COLIBRI_EVENT_TYPE_SETPOINT           0x08
#define COLIBRI_EVENT_TYPE_COMPUTED_VALUE     0x09
#define COLIBRI_EVENT_TYPE_COUNTER            0x0A
#define COLIBRI_EVENT_TYPE_ALARM_INDICATION   0x0B
#define COLIBRI_EVENT_TYPE_MIN_VALUE          0x0C
#define COLIBRI_EVENT_TYPE_MAX_VALUE          0x0D
#define COLIBRI_EVENT_TYPE_LOW_THRESHOLD      0x0E
#define COLIBRI_EVENT_TYPE_HIGH_THRESHOLD     0x0F
#define COLIBRI_EVENT_TYPE_RUN_INDICATION     0x10
#define COLIBRI_EVENT_TYPE_RGB_INDICATOR      0x11
#define COLIBRI_EVENT_TYPE_TIME               0x12
#define COLIBRI_EVENT_TYPE_MODBUS_UPDATE      0x13
#define COLIBRI_EVENT_TYPE_MQTT               0x14
#define COLIBRI_EVENT_TYPE_LUA_UPDATED        0x15
#define COLIBRI_EVENT_TYPE_END_OF_LIST        0x16

//   Event format
//
//   +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
//   | 31 | 30 | 29 | 28 | 27 | 26 | 25 | 24 | 23 | 22 | 21 | 20 | 19 | 18 | 17 | 16 | 15 | 14 | 13 | 12 | 11 | 10 |  9 |  8 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
//   +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
//   |      Slot              | IO |                Event Type                       |                            Event Parameter                                    |
//   +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
//
#define create_io_event(SLOT, EVENT_TYPE, EVENT_PARAM) (SLOT<<27 | 1<<26 | (EVENT_TYPE<<16) | EVENT_PARAM)
#define create_user_event(EVENT_TYPE, EVENT_PARAM) (EVENT_TYPE<<16 | EVENT_PARAM)

typedef union
{
    uint32_t value;
    struct
    {
        uint16_t parameter:16;
        uint16_t type:10;
        uint8_t io:1;
        uint8_t slot:5;
    };
} event_t;

typedef void (*event_callback)(event_t event, int64_t value);

// TODO: The volatile inside the event_subscription struct is a temporary measure to ensure register caching is not happening. Review and work out the "right" way.
// The issue is that I/O thread and Lua thread will share event_subscription. And there is preemption risks involved.
typedef struct
{
    event_t event_type;
    event_callback callback;
    volatile int64_t latest_value;
} event_subscription;

int events_initialize();
int64_t events_get(event_t event);
void events_publish(event_t event, int64_t value);
void* events_subscribe(event_t event, event_callback callback);
void events_unsubscribe(void* subscription);

#endif
