#include "colibri/slots.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define PWR_PARENT DT_NODELABEL(slot_ctrl_pwr)
#define RST_PARENT DT_NODELABEL(slot_ctrl_rst)

#define GPIO_SPEC_FROM_CHILD(node_id) GPIO_DT_SPEC_GET(node_id, gpios),

static const struct gpio_dt_spec pwr_pins[] = {
    DT_FOREACH_CHILD_STATUS_OKAY(PWR_PARENT, GPIO_SPEC_FROM_CHILD)
};

static const struct gpio_dt_spec rst_pins[] = {
    DT_FOREACH_CHILD_STATUS_OKAY(RST_PARENT, GPIO_SPEC_FROM_CHILD)
};

int selected_slot;
static uint32_t powered = 0;
static uint32_t not_in_reset = 0;

static int slot_set_state(const struct gpio_dt_spec* pins, size_t pin_count, uint8_t slot, bool active)
{
    if (slot >= pin_count)
    {
        return -EINVAL;
    }

    return gpio_pin_set_dt(&pins[slot], active ? 1 : 0);
}

int slots_initialize()
{
    slot_count_init();
    for (size_t i = 0; i < slot_count(); i++)
    {
        if (!gpio_is_ready_dt(&pwr_pins[i]))
        {
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(&pwr_pins[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0)
        {
            return ret;
        }
        powered |= (1 << i);
    }

    for (size_t i = 0; i < slot_count(); i++)
    {
        if (!gpio_is_ready_dt(&rst_pins[i]))
        {
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(&rst_pins[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0)
        {
            return ret;
        }
        not_in_reset |= (1 << i);
    }

    k_msleep(100); // Allow for hardware to wake up.
    for (size_t i = 0; i < slot_count(); i++)
    {
        slot_select(i);
    }
    return 0;
}

int slot_set_power_state(uint8_t slot, bool enabled)
{
    if (!enabled)
        powered &= ~(1 << slot);
    int result = slot_set_state(pwr_pins, slot_count(), slot, enabled);
    if (enabled)
        powered |= (1 << slot);
    return result;
}

int slot_set_reset_state(uint8_t slot, bool asserted)
{
    if (asserted)
        not_in_reset &= ~(1 << slot);
    int result = slot_set_state(rst_pins, slot_count(), slot, asserted);
    if (asserted)
        not_in_reset |= (1 << slot);
    return result;
}

bool slot_is_powered(uint8_t slot)
{
    return powered & 1 << slot;
}

bool slot_is_reset(uint8_t slot)
{
    return (not_in_reset & 1 << slot) == 0;
}

void slot_select(unsigned int slot)
{
    // TODO: set the I2C MUX, set the SPI CS pins, but SKIP if power is off or reset is active
    selected_slot = slot;
    if (slot_is_reset(slot) || !slot_is_powered(slot))
    {
        return;
    }
    i2c_write(, buf, sizeof(buf), SLOT_MUX)
}

int slot_selected()
{
    return selected_slot;
}
