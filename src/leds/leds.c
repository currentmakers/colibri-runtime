#include <zephyr/drivers/gpio.h>
#include "colibri/leds.h"

/*
 * Onboard LED, defined by the board .dts as aliased to led0 and led1 to support standard examples.
 */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

void led_set(int state)   // 0=off, 1=green, 2=red, 3=both
{
    switch (state)
    {
    case 1:
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 0);
        break;
    case 2:
        gpio_pin_set_dt(&led_green, 0);
        gpio_pin_set_dt(&led_red, 1);
        break;
    case 3:
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 1);
        break;
    case 0:
    default:
        gpio_pin_set_dt(&led_green, 0);
        gpio_pin_set_dt(&led_red, 0);
        break;
    }
}

void led_set_off()
{
    led_set(0);
}

void led_set_green()
{
    led_set(1);
}

void led_set_red()
{
    led_set(2);
}

void led_set_both()
{
    led_set(3);
}


int led_init(void)
{
    if (!gpio_is_ready_dt(&led_green))
    {
        printk("led_green GPIO is not ready\n");
        return -1;
    }
    if (!gpio_is_ready_dt(&led_red))
    {
        printk("led_red GPIO is not ready\n");
        return -1;
    }

    if (gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0)
    {
        printk("Failed to configure led_green\n");
        return -1;
    }

    if (gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0)
    {
        printk("Failed to configure led_red\n");
        return -1;
    }
    return 0;
}