
#include <zephyr/kernel.h>

#define SUPERVISOR_STACK_SIZE 1024
#define SUPERVISOR_PRIORITY 5

K_THREAD_STACK_DEFINE(supervisor_work_queue_stack, SUPERVISOR_STACK_SIZE);
static struct k_work_q supervisor_queue;

static struct k_work_delayable rgb_update_work;

static void supervisor_tick(struct k_work *work)
{
    printk("Supervisor tick\n");
    k_work_schedule_for_queue(&supervisor_queue, &rgb_update_work, K_MSEC(5000));
}

void supervisor_initialize()
{
    k_work_queue_start(&supervisor_queue, supervisor_work_queue_stack,
                       K_THREAD_STACK_SIZEOF(supervisor_work_queue_stack),
                       SUPERVISOR_PRIORITY, NULL);

    k_work_init_delayable(&rgb_update_work, supervisor_tick);
    k_work_schedule_for_queue(&supervisor_queue, &rgb_update_work, K_NO_WAIT);
}