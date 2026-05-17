#ifndef COLIBRI_H
#define COLIBRI_H
#include <stdint.h>
#include <stdbool.h>

// System API
#define COLIBRI_COLOR_OFF 0
#define COLIBRI_COLOR_OK (-1)
#define COLIBRI_COLOR_WARNING (-2)
#define COLIBRI_COLOR_ERROR (-3)
#define COLIBRI_COLOR_INFO (-4)

#define event_param(EVENT) (EVENT & 0xFFFF)
#define event_type(EVENT) ((EVENT & 0xFFFF0000) >> 16)
#define is_event_type(EVENT, TYPE) ((EVENT & 0x3FF0000) == (TYPE<<16))
#define is_io_event(EVENT) ((EVENT & (1<<26)) == (1<<26))
#define is_user_event(EVENT) ((EVENT & (1<<26)) == 0x0)
#define create_io_event(SLOT, EVENT_TYPE, EVENT_PARAM) (SLOT<<27 | 1<<26 | (EVENT_TYPE<<16) | EVENT_PARAM)
#define create_user_event(EVENT_TYPE, EVENT_PARAM) (EVENT_TYPE<<16 | EVENT_PARAM)

// Max 1024 EVENT_TYPEs
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
#define COLIBRI_EVENT_TYPE_RGB_INDICATOR      0x0E
#define COLIBRI_EVENT_TYPE_OUTPUT             0x0F
#define COLIBRI_EVENT_TYPE_CONFIG             0x10

#endif
