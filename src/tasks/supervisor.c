
#include <zephyr/kernel.h>

#define SUPERVISOR_STACK_SIZE 512
#define SUPERVISOR_PRIORITY 5

K_THREAD_STACK_DEFINE(supervisor_work_queue_stack, SUPERVISOR_STACK_SIZE);
static struct k_work_q supervisor_queue;

static struct k_work_delayable supervisor_update_work;

extern size_t luaGetFreeHeapSize(void);
extern size_t luaGetMinimumEverFreeHeapSize(void);

static void supervisor_tick(struct k_work *work)
{
    printk("Supervisor:\n");
    printk("    Lua heap free: %zu bytes\n", luaGetFreeHeapSize());
    printk("    Lua heap min free: %zu bytes\n", luaGetMinimumEverFreeHeapSize());
    k_work_schedule_for_queue(&supervisor_queue, &supervisor_update_work, K_MSEC(5000));
}

int supervisor_initialize()
{
    k_work_queue_start(&supervisor_queue, supervisor_work_queue_stack,
                       K_THREAD_STACK_SIZEOF(supervisor_work_queue_stack),
                       SUPERVISOR_PRIORITY, NULL);

    k_work_init_delayable(&supervisor_update_work, supervisor_tick);
    int result = k_work_schedule_for_queue(&supervisor_queue, &supervisor_update_work, K_NO_WAIT);
    return result < 0 ? result : 0;
}