/*
 * Copyright (c) 2024 CurrentMakers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <string.h>

#include "colibri-sdk/colibri-events.h"
#include "colibri/events.h"
#include "colibri/fs.h"
#include "colibri/leds.h"
#include "colibri/luaInterface.h"
#include "colibri/management.h"
#include "colibri/modbus.h"
#include "colibri/slots.h"
#include "colibri/tasks.h"
#include "colibri/usb.h"

#define BLINK_INTERVAL_MS 300

static void publish_time_period_event(int64_t time_since_start, int period)
{
    event_t ev = { .value = create_user_event(COLIBRI_EVENT_TYPE_TIME_PERIOD, period) };
    events_publish_isr(ev, time_since_start);
}

static void tick_1ms_expiry(struct k_timer *timer)
{
    int64_t value = k_uptime_get();
    // We are only going to support 1ms, 10ms, 100ms and 1 second TIME_PERIODs. Anything longer than that, use EVENT_TYPE_TIME
    int32_t small_clock = (int32_t) value; // TODO: We will accept a hiccup of 648ms, once every 24 days
    if ( small_clock % 1000 == 0 )
    {
        publish_time_period_event(value, 1000);
        publish_time_period_event(value, 100);
        publish_time_period_event(value, 10);
        publish_time_period_event(value, 1);
    } else if ( small_clock % 100 == 0 )
    {
        publish_time_period_event(value, 100);
        publish_time_period_event(value, 10);
        publish_time_period_event(value, 1);
    } else if ( small_clock % 10 == 0 )
    {
        publish_time_period_event(value, 10);
        publish_time_period_event(value, 1);
    } else
    {
        publish_time_period_event(value, 1);
    }
}

K_TIMER_DEFINE(tick_1ms, tick_1ms_expiry, NULL);


static void list_directory(const char* path)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;
    int rc;

    fs_dir_t_init(&dir);
    rc = fs_opendir(&dir, path);
    if (rc < 0)
    {
        printk("Failed to open directory %s: %d\n", path, rc);
        return;
    }

    while (1)
    {
        rc = fs_readdir(&dir, &entry);
        if (rc < 0)
        {
            printk("Failed to read directory %s: %d\n", path, rc);
            break;
        }
        if (entry.name[0] == 0)
        {
            break;
        }

        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry.name);

        if (entry.type == FS_DIR_ENTRY_DIR)
        {
            printk("  %s/\n", full_path);
            list_directory(full_path);
        }
        else
        {
            printk("  %s (size: %zu)\n", full_path, entry.size);
        }
    }
    fs_closedir(&dir);
}

static int initialize_all(void)
{
    int error;
    slot_count_initialize();
    error = events_initialize();
    if (error)
    {
        printk("Unable to initialize event system. Aborting boot.\n");
        return error;
    }
    error = led_initialize();
    if (error)
    {
        printk("boot: Unable to initialize LEDs. Aborting boot.\n");
        return error;
    }
    error = rgb_initialize();
    if (error)
    {
        printk("boot: Unable to initialize Neopixel LEDs. Aborting boot.\n");
        return error;
    }
    error = slots_initialize();
    if (error)
    {
        printk("boot: Unable to initialize slots: %d. Aborting boot.\n", error);
        return error;
    }
    error = nvs_initialize();
    if (error)
    {
        printk("boot: Unable to initialize NVS: %d. Aborting boot.\n", error);
        return error;
    }
    error = modbus_initialize();
    if (error)
    {
        printk("boot: Unable to initialize Modbus: %d. Aborting boot.\n", error);
        return error;
    }
    printk("Colibri Runtime booting on board '%s'\n", CONFIG_BOARD);
    int number_of_slots = slot_count();
    for (int i = 0; i < number_of_slots; i++)
        rgb_set_off(i);

    printk("Listing directories\n");
    list_directory("/carrier");
    list_directory("/mcu");
    printk("\n");

    // TODO: Maybe figure out if we can enable RTC and have an actual date/time in there as well.
    printk("Writing start note to carrier board\n");
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, "/mcu/startlog.txt", FS_O_RDWR | FS_O_CREATE | FS_O_APPEND);
    if (rc == 0)
    {
        char entry[] = "Start log entry.\n";
        fs_write(&file, entry, strlen(entry));
        fs_close(&file);
    }
    else
    {
        printk("Unable to write to start log.\n");
    }
    error = supervisor_initialize();
    if (error)
    {
        printk("Unable to initialize supervisor. Aborting boot.\n");
        return error;
    }
    error = io_initialize(number_of_slots);
    if (error)
    {
        printk("Unable to initialize IO. Aborting boot.\n");
        return error;
    }
    error = usb_initialize();
    if (error != 0) {
        printk("Failed to enable USB stack. Ignoring.\n");
    }
    error = management_initialize();
    if (error != 0) {
        printk("Failed to enable management. Ignoring.\n");
    }

    error = lua_initialize();
    if (error)
    {
        printk("Unable to initialize Lua. Aborting boot.\n");
    }
    return error;
}


void register_timer_event(void)
{
    k_timer_start(&tick_1ms, K_MSEC(1), K_MSEC(1));
}

int main()
{
    int error = initialize_all();

    register_timer_event();

    // TODO: have a better error system, so LEDs can indicate what is wrong. E.g No user script = yellow blink.
    if (error)
    {
        led_set_red();
        return error;
    }
    for (int i = 0; i < 6; i++)
    {
        if (i % 2)
            led_set_green();
        else
            led_set_off();
        k_sleep(K_MSEC(100));
    }
    led_set_off();
    return 0;
}
