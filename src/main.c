/*
 * Copyright (c) 2024 CurrentMakers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>

#include "colibri/slots.h"
#include "colibri/fs.h"
#include "colibri/leds.h"
#include "colibri/luaInterface.h"
#include "colibri/modbus.h"
#include "colibri/tasks.h"

#define BLINK_INTERVAL_MS 300

static int initialize_all(void)
{
    int error;
    error = led_init();
    if (error)
    {
        printk("boot: Unable to initialize LEDs. Aborting boot.\n");
        return error;
    }
    error = slots_initialize();
    if (error)
    {
        printk("boot: Unable to initialize slots: %d. Aborting boot.\n", error);
        return error;
    }
    error = nvs_init();
    if (error)
    {
        printk("boot: Unable to initialize NVS: %d. Aborting boot.\n", error);
        return error;
    }
    error = littlefs_init();
    if (error)
    {
        printk("boot: Unable to initialize LittleFS: %d. Aborting boot.\n", error);
        return error;
    }
    error = modbus_init();
    if (error)
    {
        printk("boot: Unable to initialize Modbus: %d. Aborting boot.\n", error);
        return error;
    }
    printk("Colibri Runtime booting on board '%s'\n", CONFIG_BOARD);
    rgb_set_off(0);
    rgb_set_off(1);
    rgb_set_off(2);
    rgb_set_off(3);
    rgb_set_off(4);
    rgb_set_off(5);
    rgb_set_off(6);
    rgb_set_off(7);

    printk("Writing start note to carrier board\n");
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, "/carrier/startlog.txt", FS_O_CREATE | FS_O_APPEND);
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
    error = events_initialize();
    if (error)
    {
        printk("Unable to initialize event system. Aborting boot.");
        return error;
    }
    error = supervisor_initialize();
    if (error)
    {
        printk("Unable to initialize supervisor. Aborting boot.");
        return error;
    }
    error = io_initialize(slot_count());
    if (error)
    {
        printk("Unable to initialize IO. Aborting boot.");
        return error;
    }
    error = lua_initialize();
    if (error)
    {
        printk("Unable to initialize Lua. Aborting boot.");
    }
    return error;
}

int main()
{
    int error = initialize_all();
    if ( error)
    {
        led_set_red();
        return error;
    }
    for ( int i=0 ; i < 6; i++)
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