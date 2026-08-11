#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/sys/sys_heap.h>
#include "colibri/supervisor.h"

/* The heap backing k_malloc()/k_free(), defined by the kernel when
 * CONFIG_HEAP_MEM_POOL_SIZE > 0. */
extern struct k_heap _system_heap;

K_THREAD_STACK_DEFINE(supervisor_work_queue_stack, SUPERVISOR_STACK_SIZE);
static struct k_work_q supervisor_queue;

static struct k_work_delayable supervisor_update_work;
static struct k_work_queue_config config = {
    .name = "supervisor",
    .no_yield = false,
    .essential = false,
    .work_timeout_ms = 0
};

extern size_t luaGetFreeHeapSize(void);
extern size_t luaGetMinimumEverFreeHeapSize(void);
extern void* luaGetHeapBase(void);
extern size_t luaGetHeapTotalSize(void);

static uint64_t last_timing = 0;
static timing_t timings[SUPERVISOR_TIMING_END];

static void thread_cb(const struct k_thread* thread, void* user_data)
{
    ARG_UNUSED(user_data);

    k_tid_t t_id = (k_tid_t)thread;
    const char* name = k_thread_name_get(t_id);

#ifdef CONFIG_THREAD_STACK_INFO
    uintptr_t base = thread->stack_info.start;
    size_t size = thread->stack_info.size;
    size_t end = base + size;

    /* Peak stack usage ("high water mark"): how many bytes this thread has
     * ever touched. If used approaches size, that thread is the overflow
     * suspect. Requires CONFIG_INIT_STACKS=y for the sentinel fill. */
    size_t unused = 0;
    size_t used = size;
    if (k_thread_stack_space_get(thread, &unused) == 0)
    {
        used = size - unused;
    }
    k_thread_runtime_stats_t stats;
    k_thread_runtime_stats_get(t_id, &stats);

    printk("  %-14s %d%%, stack [0x%08lx-0x%08lx] size %5zu used %5zu (%zu%% free)\n",
           name ? name : "unnamed",
           (uint8_t)(stats.total_cycles * 100 / stats.execution_cycles),
           (unsigned long)base, (unsigned long)end,
           size, used,
           size ? unused * 100U / size : 0U);
#else
    printk("    thread %-16s (enable CONFIG_THREAD_STACK_INFO for stack base/size)\n",
           name ? name : "unnamed");
#endif
}

static void supervisor_tick(struct k_work* work)
{
    uintptr_t heap_base = (uintptr_t)luaGetHeapBase();
    size_t heap_size = luaGetHeapTotalSize();

    printk("Supervisor:\n");
    printk("  Lua\n"
           "      heap [0x%08lx-0x%08lx] size %zu bytes, free %zu, min free %zu\n",
           (unsigned long)heap_base,
           (unsigned long)(heap_base + heap_size),
           heap_size,
           luaGetFreeHeapSize(),
           luaGetMinimumEverFreeHeapSize());
    /* System heap backing k_malloc(). init_mem/init_bytes and the runtime
     * stats are available because CONFIG_SYS_HEAP_RUNTIME_STATS=y. */
    struct sys_heap* sys = &_system_heap.heap;
    uintptr_t sys_base = (uintptr_t)sys->init_mem;
    size_t sys_size = sys->init_bytes;
    struct sys_memory_stats mem_stats = {0};

    sys_heap_runtime_stats_get(sys, &mem_stats);
    printk("  Sys \n      heap [0x%08lx-0x%08lx] size %zu bytes, free %zu, allocated %zu, peak  %zu\n",
           (unsigned long)sys_base,
           (unsigned long)(sys_base + sys_size),
           sys_size,
           mem_stats.free_bytes,
           mem_stats.allocated_bytes,
           mem_stats.max_allocated_bytes);

    k_thread_foreach_unlocked(thread_cb, NULL);
    uint64_t now = k_cycle_get_64();
    uint64_t total_cycles = now - last_timing;
    printk("Timings, cycles=%lld\n", total_cycles);
    // TODO We are getting about cycles=2160247726, which at 168MHz is only 12.8 seconds
    //      but the report is executing every 30 seconds (measured externally to be correct).
    //      So "cycles" or only 40% of the actual cycles?

    for (int i = 0; i < SUPERVISOR_TIMING_END; i++)
    {
        timing_t *timing = &timings[i];
        uint16_t samples = timing->samples;
        if ( samples > 0 )
        {
            uint32_t avg_cycles = timing->cycles_sum / samples;
            uint32_t cycles_per_sec = timing->cycles_sum / 30;
            printk("    %s - Calls: %d, average %6d cycles/call, %6d cycles/sec\n", timing->name, samples, avg_cycles, cycles_per_sec);
            timing->cycles_sum = 0;
            timing->samples = 0;
        }
    }
    last_timing = now;
    k_work_schedule_for_queue(&supervisor_queue, &supervisor_update_work, K_MSEC(30000));
}

static void timing_init(const int index, const char* name)
{
    timings[index].name = name;
    timings[index].cycles_sum = 0;
    timings[index].samples = 0;
}

int supervisor_initialize()
{
    timing_init(SUPERVISOR_TIMING_LUA, "Lua");
    timing_init(SUPERVISOR_TIMING_IO0,"I/O slot 0");
    timing_init(SUPERVISOR_TIMING_IO1,"I/O slot 1");
    timing_init(SUPERVISOR_TIMING_IO2,"I/O slot 2");
    timing_init(SUPERVISOR_TIMING_IO3,"I/O slot 3");
    timing_init(SUPERVISOR_TIMING_IO4,"I/O slot 4");
    timing_init(SUPERVISOR_TIMING_IO5,"I/O slot 5");
    timing_init(SUPERVISOR_TIMING_IO6,"I/O slot 6");
    timing_init(SUPERVISOR_TIMING_IO7,"I/O slot 7");
    k_work_queue_start(&supervisor_queue, supervisor_work_queue_stack,
                       K_THREAD_STACK_SIZEOF(supervisor_work_queue_stack),
                       SUPERVISOR_PRIORITY, &config);

    k_work_init_delayable(&supervisor_update_work, supervisor_tick);
    int result = k_work_schedule_for_queue(&supervisor_queue, &supervisor_update_work, K_NO_WAIT);
    return result < 0 ? result : 0;
}

void supervisor_timing_start(int index)
{
    if (index < 0 || index >= SUPERVISOR_TIMING_END)
        return;
    timings[index].start = k_cycle_get_32();
}

void supervisor_timing_stop(int index)
{
    if (index < 0 || index >= SUPERVISOR_TIMING_END)
        return;
    timing_t* timing = &timings[index];
    timing->cycles_sum += k_cycle_get_32() - timing->start;
    timing->samples++;
}
