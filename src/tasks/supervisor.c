

#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/sys/sys_heap.h>

/* The heap backing k_malloc()/k_free(), defined by the kernel when
 * CONFIG_HEAP_MEM_POOL_SIZE > 0. */
extern struct k_heap _system_heap;

#define SUPERVISOR_STACK_SIZE 1024
#define SUPERVISOR_PRIORITY 5

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
extern void  *luaGetHeapBase(void);
extern size_t luaGetHeapTotalSize(void);


static void thread_cb(const struct k_thread *thread, void *user_data)
{
    ARG_UNUSED(user_data);

    const char *name = k_thread_name_get((k_tid_t)thread);

#ifdef CONFIG_THREAD_STACK_INFO
    uintptr_t base = thread->stack_info.start;
    size_t    size = thread->stack_info.size;
    size_t    end  = base + size;

    /* Peak stack usage ("high water mark"): how many bytes this thread has
     * ever touched. If used approaches size, that thread is the overflow
     * suspect. Requires CONFIG_INIT_STACKS=y for the sentinel fill. */
    size_t unused = 0;
    size_t used   = size;
    if (k_thread_stack_space_get(thread, &unused) == 0) {
        used = size - unused;
    }

    printk("    thread %-16s stack [0x%08lx-0x%08lx] size %5zu used %5zu (%zu%% free)\n",
           name ? name : "unnamed",
           (unsigned long)base, (unsigned long)end,
           size, used,
           size ? (unused * 100U) / size : 0U);
#else
    printk("    thread %-16s (enable CONFIG_THREAD_STACK_INFO for stack base/size)\n",
           name ? name : "unnamed");
#endif
}

static void supervisor_tick(struct k_work *work)
{
    uintptr_t heap_base = (uintptr_t)luaGetHeapBase();
    size_t    heap_size = luaGetHeapTotalSize();

    printk("Supervisor:\n");
    printk("    Lua heap [0x%08lx-0x%08lx] size %zu bytes, free %zu, min free %zu\n",
           (unsigned long)heap_base,
           (unsigned long)(heap_base + heap_size),
           heap_size,
           luaGetFreeHeapSize(),
           luaGetMinimumEverFreeHeapSize());

    /* System heap backing k_malloc(). init_mem/init_bytes and the runtime
     * stats are available because CONFIG_SYS_HEAP_RUNTIME_STATS=y. */
    {
        struct sys_heap *sys = &_system_heap.heap;
        uintptr_t sys_base = (uintptr_t)sys->init_mem;
        size_t    sys_size = sys->init_bytes;
        struct sys_memory_stats stats = {0};

        sys_heap_runtime_stats_get(sys, &stats);
        printk("    Sys heap [0x%08lx-0x%08lx] size %zu bytes, free %zu, allocated %zu, peak  %zu\n",
               (unsigned long)sys_base,
               (unsigned long)(sys_base + sys_size),
               sys_size,
               stats.free_bytes,
               stats.allocated_bytes,
               stats.max_allocated_bytes);
    }

    k_thread_foreach_unlocked(thread_cb, NULL);
    k_work_schedule_for_queue(&supervisor_queue, &supervisor_update_work, K_MSEC(5000));
}

int supervisor_initialize()
{
    k_work_queue_start(&supervisor_queue, supervisor_work_queue_stack,
                       K_THREAD_STACK_SIZEOF(supervisor_work_queue_stack),
                       SUPERVISOR_PRIORITY, &config);

    k_work_init_delayable(&supervisor_update_work, supervisor_tick);
    int result = k_work_schedule_for_queue(&supervisor_queue, &supervisor_update_work, K_NO_WAIT);
    return result < 0 ? result : 0;
}
