
#include "colibri/slots.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

#define GPIO_SPEC_FROM_CHILD(node_id) GPIO_DT_SPEC_GET(node_id, gpios),
#define SLOT_COUNT_NODE DT_NODELABEL(slot_count)
static const struct gpio_dt_spec slot_pins[] = {
    DT_FOREACH_CHILD(SLOT_COUNT_NODE, GPIO_SPEC_FROM_CHILD)
};

static int number_of_slots = 0;

int slot_count()
{
    return number_of_slots;
}

void slot_count_init() {
    number_of_slots = 0;
    for (int i = 0; i < ARRAY_SIZE(slot_pins); i++) {
        gpio_pin_configure_dt(&slot_pins[i], GPIO_INPUT);
        if (gpio_pin_get_dt(&slot_pins[i]) > 0) {
            number_of_slots |= (1 << i);
        }
    }
    printk("Slots on carrier: %d\n", number_of_slots);
}
