/*
 * Copyright (c) 2024 CurrentMakers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <string.h>

#include "colibri/slots.h"
#include "colibri/fs.h"
#include "colibri/leds.h"
#include "colibri/luaInterface.h"
#include "colibri/modbus.h"
#include "colibri/tasks.h"
#include "colibri/usb.h"

#define BLINK_INTERVAL_MS 300

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

    printk("Listing directory: %s\n", path);
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
            printk("  [DIR]  %s\n", full_path);
            list_directory(full_path);
        }
        else
        {
            printk("  [FILE] %s (size: %zu)\n", full_path, entry.size);
        }
    }
    fs_closedir(&dir);
}

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
    // error = littlefs_init();
    // if (error)
    // {
    //     printk("boot: Unable to initialize LittleFS: %d. Aborting boot.\n", error);
    //     return error;
    // }
    error = modbus_init();
    if (error)
    {
        printk("boot: Unable to initialize Modbus: %d. Aborting boot.\n", error);
        return error;
    }
    printk("Colibri Runtime booting on board '%s'\n", CONFIG_BOARD);
    int number_of_slots = slot_count();
    for (int i = 0; i < number_of_slots; i++)
        rgb_set_off(i);

    printk("Listing /carrier directory:\n");
    list_directory("/carrier");
    printk("\nListing /mcu directory:\n");
    list_directory("/mcu");
    printk("\n");

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
    error = events_initialize();
    if (error)
    {
        printk("Unable to initialize event system. Aborting boot.\n");
        return error;
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
        printk("Failed to enable USB stack. Aborting boot.\n");
        return error;
    }

    error = lua_initialize();
    if (error)
    {
        printk("Unable to initialize Lua. Aborting boot.\n");
    }
    return error;
}

int main()
{
    int error = initialize_all();
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
