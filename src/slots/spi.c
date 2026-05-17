/*
 * Copyright (c) 2025 CurrentMakers
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI glue for slot drivers.
 *
 * Hardware overview
 * -----------------
 * The carrier exposes one STM32 SPI bus (spi1) shared by all slots.
 * spi1 already knows about every slot's chip-select line via its
 * cs-gpios property; the i-th entry of that array is what Zephyr will
 * drive when a transfer is issued with `spi_config.slave = i`.
 *
 * For our carrier:
 *   cs-gpios[0] = &gpiob 2         -> on-board MCU3 flash
 *   cs-gpios[1] = tca6424 P20      -> carrier flash               (slot 0)
 *   cs-gpios[2] = tca6424 P21      -> slot 1
 *   cs-gpios[3] = tca6424 P22      -> slot 2
 *   ...
 *   cs-gpios[8] = tca6424 P27      -> slot 7
 *
 *   => slave index for slot N is (N + 1).
 *
 * On top of that, each I/O module can carry up to four SPI sub-devices.
 * Which one of the four is addressed is decoded on the I/O module from
 * two select lines that the carrier drives in parallel to all slots:
 *
 *   PA4 -> slot_spi_sel0   (bit 0 of the sub-device address)
 *   PA8 -> slot_spi_sel1   (bit 1 of the sub-device address)
 *
 * Per-transaction sequence:
 *   1. Drive {sel1, sel0} with the 2-bit "address" argument (0..3).
 *   2. Issue a spi_transceive() on spi1 with slave = slot + 1, which
 *      makes Zephyr assert that slot's CS for the duration of the
 *      transfer and deassert it at the end.
 *   3. Leave the select lines alone; they'll be overwritten on the next
 *      transfer.
 *
 * Driver-facing API (see colibri-io.h, Host_API_t):
 *
 *   void spi_write(uint8_t address, uint8_t *data, uint8_t length);
 *   void spi_read (uint8_t address, uint8_t *to_write,
 *                  uint8_t *to_read, uint8_t length);
 *
 * `address` is the 2-bit sub-device id; the slot is implicit via
 * slot_selected().
 */

#include "colibri/slots.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

/* -------------------------------------------------------------------- *
 *  The raw spi1 handle + per-slot spi_config                            *
 * -------------------------------------------------------------------- */

#define SLOT_SPI_BUS_NODE DT_NODELABEL(spi1)

static const struct device *const slot_spi_bus =
    DEVICE_DT_GET(SLOT_SPI_BUS_NODE);

/* slot N -> cs-gpios index (N + 1); see the comment above. */
#define SLOT_SLAVE_INDEX(slot) ((uint16_t)((slot) + 1))

/*
 * Conservative defaults that suit most peripherals. If a specific
 * sub-device ever needs a different freq/mode, expose a setter and
 * fix it up per (slot, sub-device) at module load time.
 */
#define SLOT_SPI_DEFAULT_FREQ_HZ  1000000U
#define SLOT_SPI_DEFAULT_OPERATION \
    (SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8))

/*
 * One spi_config per slot. We hand a stable pointer to spi_transceive()
 * each time, which lets the Zephyr SPI driver use the standard pointer-
 * identity trick to skip a re-init when nothing has changed.
 */
static struct spi_config slot_spi_cfg[MAX_SLOTS];

/* -------------------------------------------------------------------- *
 *  The two on-board sub-device select lines (PA4, PA8)                  *
 * -------------------------------------------------------------------- */

static const struct gpio_dt_spec sel_pins[2] = {
    GPIO_DT_SPEC_GET(DT_NODELABEL(slot_spi_sel0), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(slot_spi_sel1), gpios),
};

/* -------------------------------------------------------------------- *
 *  One-time init                                                        *
 * -------------------------------------------------------------------- */

static bool slot_spi_initialized;

static int slot_spi_init(void)
{
    if (slot_spi_initialized) {
        return 0;
    }

    if (!device_is_ready(slot_spi_bus)) {
        printk("spi: spi1 not ready\n");
        return -ENODEV;
    }

    for (int slot = 0; slot < MAX_SLOTS; slot++) {
        slot_spi_cfg[slot].frequency = SLOT_SPI_DEFAULT_FREQ_HZ;
        slot_spi_cfg[slot].operation = SLOT_SPI_DEFAULT_OPERATION;
        slot_spi_cfg[slot].slave     = SLOT_SLAVE_INDEX(slot);
        /* cs.{gpio,delay,cs_is_gpio} stay zero -> Zephyr uses the
         * cs-gpios entry indexed by `slave` from the spi1 node. */
    }

    for (size_t i = 0; i < ARRAY_SIZE(sel_pins); i++) {
        if (!gpio_is_ready_dt(&sel_pins[i])) {
            printk("spi: sel pin %u not ready\n", (unsigned)i);
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(&sel_pins[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            printk("spi: configure sel pin %u failed (%d)\n",
                   (unsigned)i, ret);
            return ret;
        }
    }

    slot_spi_initialized = true;
    return 0;
}

/* -------------------------------------------------------------------- *
 *  Sub-device select                                                    *
 * -------------------------------------------------------------------- */

/*
 * Drive PA4:PA8 to encode the 2-bit sub-device address.
 */
static int slot_spi_set_subdevice(uint8_t address)
{
    if (address > 3) {
        return -EINVAL;
    }

    int ret = gpio_pin_set_dt(&sel_pins[0], (address >> 0) & 0x1);
    if (ret < 0) {
        return ret;
    }
    return gpio_pin_set_dt(&sel_pins[1], (address >> 1) & 0x1);
}

/* -------------------------------------------------------------------- *
 *  Helpers                                                              *
 * -------------------------------------------------------------------- */

static struct spi_config *current_slot_cfg(int *out_slot)
{
    if (!slot_spi_initialized && slot_spi_init() < 0) {
        return NULL;
    }

    int slot = slot_selected();
    if (slot < 0 || slot >= MAX_SLOTS) {
        return NULL;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return &slot_spi_cfg[slot];
}

/* -------------------------------------------------------------------- *
 *  Host_API_t back-ends                                                 *
 * -------------------------------------------------------------------- */

/*
 * Clock `length` bytes from `data` to sub-device `address` on the I/O
 * module currently selected via slot_selected(). MISO is discarded.
 */
void slot_spi_write(uint8_t address, uint8_t *data, uint16_t length)
{
    int slot;
    struct spi_config *cfg = current_slot_cfg(&slot);
    if (cfg == NULL) {
        printk("spi_write: no config for slot %d\n", slot_selected());
        return;
    }

    int ret = slot_spi_set_subdevice(address);
    if (ret < 0) {
        printk("spi_write: slot %d sub %u select failed (%d)\n",
               slot, address, ret);
        return;
    }

    const struct spi_buf tx_buf = { .buf = data, .len = length };
    const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

    ret = spi_write(slot_spi_bus, cfg, &tx);
    if (ret < 0) {
        printk("spi_write: slot %d sub %u len %u failed (%d)\n",
               slot, address, length, ret);
    }
}

/*
 * Full-duplex transfer: clock `length` bytes from `to_write` out on
 * MOSI while capturing `length` bytes into `to_read` from MISO.
 * `to_write` may be NULL, in which case the SPI controller's default
 * overrun byte is shifted out.
 */
void slot_spi_read(uint8_t address, uint8_t *to_write, uint8_t *to_read,
                   uint16_t length)
{
    int slot;
    struct spi_config *cfg = current_slot_cfg(&slot);
    if (cfg == NULL) {
        printk("spi_read: no config for slot %d\n", slot_selected());
        return;
    }
    if (to_read == NULL) {
        return;
    }

    int ret = slot_spi_set_subdevice(address);
    if (ret < 0) {
        printk("spi_read: slot %d sub %u select failed (%d)\n",
               slot, address, ret);
        return;
    }

    const struct spi_buf tx_buf = { .buf = to_write, .len = length };
    const struct spi_buf_set tx = {
        .buffers = (to_write != NULL) ? &tx_buf : NULL,
        .count   = (to_write != NULL) ? 1 : 0,
    };

    const struct spi_buf rx_buf = { .buf = to_read, .len = length };
    const struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

    ret = spi_transceive(slot_spi_bus, cfg,
                         (to_write != NULL) ? &tx : NULL,
                         &rx);
    if (ret < 0) {
        printk("spi_read: slot %d sub %u len %u failed (%d)\n",
               slot, address, length, ret);
    }
}
