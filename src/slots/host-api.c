#include "colibri/host-api.h"
#include "colibri-sdk/colibri-events.h"
#include "colibri/slots.h"


int64_t host_events_get(event_t event)
{
    return events_get(event);
}

void host_events_publish(event_t event, int64_t value)
{
    event.slot = slot_selected();
    event.io = 1;
    events_publish(event, value);
}

static void callback_to_driver(event_t event, int64_t value, int32_t user_data)
{
    slots_event_for(user_data, event, value);
}

static void* host_events_subscribe(event_t event)
{
    return events_subscribe(event, callback_to_driver, slot_selected());
}

static void host_events_unsubscribe(void* p)
{
    events_unsubscribe(p);
}

static int32_t host_slot_i2c_write(uint8_t address, uint8_t* data, uint16_t length)
{
    return slot_i2c_write(address, data, length);
}

static int32_t host_slot_i2c_read(uint8_t address, uint8_t* data, uint16_t length)
{
    return slot_i2c_read(address, data, length);
}

static int32_t host_slot_spi_write(uint8_t address, uint8_t* data, uint16_t length)
{
    return slot_spi_write(address, data, length);
}

static int32_t host_slot_spi_read(uint8_t address, uint8_t* to_write, uint8_t* to_read, uint16_t length)
{
    return slot_spi_read(address, to_write, to_read, length);
}

const Host_API_t host_api = {
    .get = host_events_get,
    .publish = host_events_publish,
    .subscribe = host_events_subscribe,
    .unsubscribe = host_events_unsubscribe,
    .i2c_write = host_slot_i2c_write,
    .i2c_read = host_slot_i2c_read,
    .spi_write = host_slot_spi_write,
    .spi_read = host_slot_spi_read
};


void hostapi_call_driver_with_r9(void* func_ptr, void* r9_target_ram, uint32_t arg1, uint32_t arg2)
{
    register uint32_t r0_val __asm__("r0") = arg1;
    register uint32_t r1_val __asm__("r1") = arg2;

    __asm volatile (
        "push {r4, r9, lr}  \n\t" // Save Host's R9 and Link Register (return address)
        "mov r9, %2         \n\t" // Load R9 with the Slot's designated RAM buffer
        "blx %3             \n\t" // Branch and Link to the driver function pointer
        "pop {r4, r9, lr}   \n\t" // Restore Host's R9 and Link Register
        : "+r"(r0_val), "+r"(r1_val)
        : "r"(r9_target_ram), "r"(func_ptr)
        : "r2", "r3", "ip", "cc", "memory"
    );
}

/*
 * Same as slot_call_driver_with_r9(), but propagates the callee's R0
 * (the AAPCS return register) back to the caller. Use this when the
 * driver function returns a value, e.g. module_entry() returning the
 * Driver_Interface_t* (vmt).
 */
uint32_t slot_call_driver_with_r9_ret(void* func_ptr, void* r9_target_ram, uint32_t arg1, uint32_t arg2)
{
    register uint32_t r0_val __asm__("r0") = arg1;
    register uint32_t r1_val __asm__("r1") = arg2;

    __asm volatile (
        "push {r4, r9, lr}  \n\t"
        "mov r9, %2         \n\t"
        "blx %3             \n\t"
        "pop {r4, r9, lr}   \n\t"
        : "+r"(r0_val), "+r"(r1_val)
        : "r"(r9_target_ram), "r"(func_ptr)
        : "r2", "r3", "ip", "cc", "memory"
    );

    return r0_val; /* AAPCS: function return value lives in R0 */
}


/*
 * 4-argument trampoline: loads r0..r3 with arg0..arg3, swaps R9 to the
 * slot's RAM, BLX's func_ptr, then restores R9 and LR.
 *
 * Note on AAPCS: a 64-bit argument must start on an even-numbered
 * register. So a function declared like
 *     void event(int32_t id, int64_t value)
 * receives:  id    in r0
 *            (r1 = padding, unused)
 *            value low  in r2
 *            value high in r3
 * Use slot_call_driver_event() below to handle that placement correctly.
 */
void slot_call_driver_with_r9_4(void* func_ptr, void* r9_target_ram,
                                       uint32_t arg0, uint32_t arg1,
                                       uint32_t arg2, uint32_t arg3)
{
    register uint32_t r0_val __asm__("r0") = arg0;
    register uint32_t r1_val __asm__("r1") = arg1;
    register uint32_t r2_val __asm__("r2") = arg2;
    register uint32_t r3_val __asm__("r3") = arg3;

    __asm volatile (
        "push {r4, r9, lr}  \n\t"
        "mov  r9, %4        \n\t"
        "blx  %5            \n\t"
        "pop  {r4, r9, lr}  \n\t"
        : "+r"(r0_val), "+r"(r1_val), "+r"(r2_val), "+r"(r3_val)
        : "r"(r9_target_ram), "r"(func_ptr)
        : "ip", "cc", "memory"
    );
}