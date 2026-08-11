#include <zephyr/kernel.h>

#include "colibri/luaInterface.h"
#include "colibri/slots.h"
#include "colibri/supervisor.h"
#include "colibri/tasks.h"

#define IO_STACK_SIZE 1024
#define IO_PRIORITY 5

K_THREAD_STACK_DEFINE(io_work_queue_stack, IO_STACK_SIZE);
static struct k_work_q io_queue;

static int n_slots = 0;
static int current_slot = 0;
static struct k_work_delayable io_work;
static struct k_work_queue_config config = {
    .name = "io-subsystem",
    .no_yield = false,
    .essential = false,
    .work_timeout_ms = 0
};

static void io_tick(struct k_work *work)
{
    int64_t now = k_uptime_get();
    slots_tick(current_slot, now);   // selects the slot under the bus lock
    current_slot = (current_slot + 1) % n_slots;
    k_work_schedule_for_queue(&io_queue, &io_work, K_MSEC(10));
}

int io_initialize()
{
    int number_of_slots= slot_count();
    n_slots = number_of_slots + 1;      // 7 means seven I/O slots, and we have 8 physical slots to deal with.
    k_work_queue_start(&io_queue, io_work_queue_stack,
                       K_THREAD_STACK_SIZEOF(io_work_queue_stack),
                       IO_PRIORITY, &config);

    k_work_init_delayable(&io_work, io_tick);
    int error = k_work_schedule_for_queue(&io_queue, &io_work, K_NO_WAIT);
    if ( error < 0 ) {
        printk("io: cannot schedule io work: %d\n", error);
        return error;
    }
    return 0;
}

void io_schedule(struct k_work_delayable* work, k_timeout_t timeout)
{
    k_work_schedule_for_queue(&io_queue, work, timeout);
}