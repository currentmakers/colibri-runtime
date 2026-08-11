#include <zephyr/kernel.h>
#include "colibri/events.h"
#include "colibri/timing.h"

static void publish_time_period_event(int64_t time_since_start, int period)
{
    event_t ev = {.type = COLIBRI_EVENT_TYPE_TIME_PERIOD, .io = false, .parameter = period, .slot = 0};
    events_publish_isr(ev, time_since_start);
}

static void tick_1ms_expiry(struct k_timer* timer)
{
    int64_t value = k_uptime_get();
    // We are only going to support 1ms, 10ms, 100ms and 1 second TIME_PERIODs. Anything longer than that, use EVENT_TYPE_TIME
    int32_t small_clock = (int32_t)value; // TODO: We will accept a hiccup of 648ms, once every 24 days
    if (small_clock % 1000 == 0)
    {
        publish_time_period_event(value, 1000);
        publish_time_period_event(value, 100);
        publish_time_period_event(value, 10);
        publish_time_period_event(value, 1);
    }
    else if (small_clock % 100 == 0)
    {
        publish_time_period_event(value, 100);
        publish_time_period_event(value, 10);
        publish_time_period_event(value, 1);
    }
    else if (small_clock % 10 == 0)
    {
        publish_time_period_event(value, 10);
        publish_time_period_event(value, 1);
    }
    else
    {
        publish_time_period_event(value, 1);
    }
}

K_TIMER_DEFINE(tick_1ms, tick_1ms_expiry, NULL);

int timing_initialize()
{
    k_timer_start(&tick_1ms, K_MSEC(20), K_MSEC(1));
    return 0;
}
