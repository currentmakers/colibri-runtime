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
#include "colibri/modbus.h"
#include "colibri/tasks.h"

#define BLINK_INTERVAL_MS 300

int main(void)
{
    int code;
    code = led_init();
    code |= nvs_init();
    code |= littlefs_init();
    code |= modbus_init();
    code |= slots_initialize();
    // wasm_initialize(slot_count());
    printk("Colibri Runtime booting on board '%s'\n", CONFIG_BOARD);
    k_msleep(1000);
    if (code != 0)
    {
        rgb_set_red(0);
        return -EINVAL;
    }
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
    k_msleep(100);
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
    supervisor_initialize();
    io_initialize(slot_count());

    // printk("Entering blink loop.\n");
    // int current_slot = 0;   // 0 = MCU slot.
    // int total_slots = slot_count() + 1;
    // // int total_slots = 2;
    // uint64_t next = 0;
    // uint64_t next_print = 0;
    // int loop_count = 0;
    // while (1)
    // {
    //     int64_t now = k_uptime_get();
    //     if ( next < now )
    //     {
    //         int64_t t0 = k_uptime_get();
    //         wasm_io_tick(now, current_slot);
    //         int64_t t1 = k_uptime_get();
    //         printk("wasm_tick took %d ms\n", (int)(t1 - t0));
    //         current_slot = (current_slot + 1) % total_slots;
    //         next = now + 50;
    //     }
    //     if ( next_print < now)
    //     {
    //         printk("Loop count: %d\n", loop_count);
    //         loop_count = 0;
    //         next_print = now + 5000;
    //     }
    //     loop_count++;
    //     k_msleep(2);
    // }
    return 0;
}
