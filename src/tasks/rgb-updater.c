#include <zephyr/kernel.h>
#include "colibri/leds.h"
#include "colibri/tasks.h"

static struct k_work_delayable rgb_update_work;

void rgb_updater_task(struct k_work* work)
{
    rgb_update();
    io_schedule(&rgb_update_work, K_MSEC(20));
}

void rgb_updater_initialize()
{
    k_work_init_delayable(&rgb_update_work, rgb_updater_task);
    io_schedule(&rgb_update_work, K_MSEC(200));
}
