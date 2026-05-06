#include "colibri/slots.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
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

static int slot_set_state(const struct gpio_dt_spec *pins, size_t pin_count, uint8_t slot, bool active)
{
	if (slot >= pin_count) {
		return -EINVAL;
	}

	return gpio_pin_set_dt(&pins[slot], active ? 1 : 0);
}

int slots_init(void)
{
	slot_count_init();
	for (size_t i = 0; i < slot_count(); i++) {
		if (!gpio_is_ready_dt(&pwr_pins[i])) {
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(&pwr_pins[i], GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	for (size_t i = 0; i < slot_count(); i++) {
		if (!gpio_is_ready_dt(&rst_pins[i])) {
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(&rst_pins[i], GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

int slot_set_power_state(uint8_t slot, bool enabled)
{
	return slot_set_state(pwr_pins, slot_count(), slot, enabled);
}

int slot_set_reset_state(uint8_t slot, bool asserted)
{
	return slot_set_state(rst_pins, slot_count(), slot, asserted);
}

void slot_select(int slot)
{
	// TODO: set the I2C MUX, set the SPI CS pins, but SKIP if power is off or reset is active
	selected_slot = slot;
}

int slot_selected()
{
	return selected_slot;
}
