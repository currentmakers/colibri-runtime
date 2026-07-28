/*
 * Copyright (c) 2025 CurrentMakers
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C glue for slot drivers.
 *
 * The carrier board has a TCA9548A 8-channel I2C multiplexer at 0x70 on
 * i2c1. Each downstream channel (i2c_chan0..i2c_chan7) is modelled in the
 * device tree as its own Zephyr i2c bus. Channel N is the I2C bus reaching
 * slot N (channel 0 is the carrier board itself, channels 1..7 are the
 * seven slots).
 */

#include "colibri/slots.h"
#include "colibri/i2c.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

/* One device pointer per TCA9548A downstream channel.
 *
 * Index N is "the I2C bus that reaches slot N":
 *   slot_i2c_bus[0] -> i2c_chan0 (carrier-local devices)
 *   slot_i2c_bus[1] -> i2c_chan1 (slot 1)
 *   ...
 *   slot_i2c_bus[7] -> i2c_chan7 (slot 7)
 */
#define I2C_CHAN_DEV(n) DEVICE_DT_GET(DT_NODELABEL(i2c_chan##n))

static const struct device *const slot_i2c_bus[MAX_SLOTS] = {
    I2C_CHAN_DEV(0),
    I2C_CHAN_DEV(1),
    I2C_CHAN_DEV(2),
    I2C_CHAN_DEV(3),
    I2C_CHAN_DEV(4),
    I2C_CHAN_DEV(5),
    I2C_CHAN_DEV(6),
    I2C_CHAN_DEV(7),
};

/*
 * Resolve the i2c bus device for the currently selected slot.
 * Returns NULL on out-of-range slot or if the bus isn't ready.
 */
static const struct device *current_slot_i2c_bus(void)
{
    int slot = slot_selected();

    if (slot < 0 || slot >= (int)ARRAY_SIZE(slot_i2c_bus)) {
        return NULL;
    }

    const struct device *bus = slot_i2c_bus[slot];
    if (bus == NULL || !device_is_ready(bus)) {
        return NULL;
    }
    return bus;
}

/*
 * Sends `length` bytes from `data` to 7-bit I2C address `address` on the
 * bus belonging to the currently selected slot. The TCA9548A driver will
 * transparently switch the mux to the slot's channel.
 */
void slot_i2c_write(uint8_t address, uint8_t *data, uint16_t length)
{
    const struct device *bus = current_slot_i2c_bus();
    if (bus == NULL) {
        printk("i2c_write: no bus for slot %d\n", slot_selected());
        return;
    }

    int ret = i2c_write(bus, data, length, address);
    if (ret < 0) {
        printk("i2c_write: slot %d addr 0x%02x len %u failed (%d)\n",
               slot_selected(), address, length, ret);
    }
}

/*
 * Reads `length` bytes into `data` from 7-bit I2C address `address` on
 * the bus belonging to the currently selected slot.
 */
void slot_i2c_read(uint8_t address, uint8_t *data, uint16_t length)
{
    const struct device *bus = current_slot_i2c_bus();
    if (bus == NULL) {
        printk("i2c_read: no bus for slot %d\n", slot_selected());
        return;
    }

    int ret = i2c_read(bus, data, length, address);
    if (ret < 0) {
        printk("i2c_read: slot %d addr 0x%02x len %u failed (%d)\n",
               slot_selected(), address, length, ret);
    }
}

int i2c_detect()
{
    const struct device* bus = slot_i2c_bus[slot_selected()];
    if (!device_is_ready(bus))
    {
        return -ENODEV;
    }

    printk("I2C scan on slot %u:\n", slot_selected());
    printk("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    int found = 0;
    for (uint8_t row = 0; row < 0x80; row += 0x10)
    {
        printk("%02x:", row);
        for (uint8_t col = 0; col < 0x10; col++)
        {
            uint8_t addr = row + col;

            // Valid 7-bit addressable range is 0x08..0x77; skip reserved ends.
            if (addr < 0x08 || addr > 0x77)
            {
                printk("   ");
                continue;
            }

            uint8_t dummy;
            struct i2c_msg msg = {
                .buf = &dummy,
                .len = 1,
                .flags = I2C_MSG_READ | I2C_MSG_STOP,
            };

            if (i2c_transfer(bus, &msg, 1, addr) == 0)
            {
                printk(" %02x", addr);
                found++;
            }
            else
            {
                printk(" --");
            }
        }
        printk("\n");
    }
    return found;
}

