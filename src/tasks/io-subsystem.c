#include <zephyr/kernel.h>
#include "colibri/slots.h"
#include "colibri/tasks.h"
#include "colibri/wasm.h"

#define IO_STACK_SIZE 4096
#define IO_PRIORITY 5

K_THREAD_STACK_DEFINE(io_work_queue_stack, IO_STACK_SIZE);
static struct k_work_q io_queue;

static int n_slots = 0;
static int current_slot = 0;
static struct k_work_delayable io_work;

static void user_tick(uint64_t now)
{
    wasm_user_tick(now);
}

static void io_tick(struct k_work *work)
{
    uint64_t now = k_uptime_get();
    slot_select(current_slot);
    if ( current_slot == 0)
    {
        // Special handling for slot 0
        user_tick(now);
    } else
    {
        wasm_io_tick(now, current_slot);
    }
    current_slot = (current_slot + 1) % n_slots;
    k_work_schedule_for_queue(&io_queue, &io_work, K_MSEC(10));
}

void io_initialize(int number_of_slots)
{
    n_slots = number_of_slots + 1;      // 7 means seven I/O slots, and we have 8 physical slots to deal with.
    k_work_queue_start(&io_queue, io_work_queue_stack,
                       K_THREAD_STACK_SIZEOF(io_work_queue_stack),
                       IO_PRIORITY, NULL);

    k_work_init_delayable(&io_work, io_tick);
    k_work_schedule_for_queue(&io_queue, &io_work, K_NO_WAIT);
    rgb_updater_initialize();
}

void io_schedule(struct k_work_delayable* work, k_timeout_t timeout)
{
    k_work_schedule_for_queue(&io_queue, work, timeout);
}