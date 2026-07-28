#include "colibri-sdk/colibri.h"
#include "colibri-sdk/colibri-io-eeprom.h"
#include "colibri/slots.h"
#include "colibri/i2c.h"

#include <stdlib.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define PWR_PARENT DT_NODELABEL(slot_ctrl_pwr)
#define RST_PARENT DT_NODELABEL(slot_ctrl_rst)

#define GPIO_SPEC_FROM_CHILD(node_id) GPIO_DT_SPEC_GET(node_id, gpios),

static void slot_call_driver_with_r9(void* func_ptr, void* r9_target_ram, uint32_t arg1, uint32_t arg2);
static uint32_t slot_call_driver_with_r9_ret(void* func_ptr, void* r9_target_ram, uint32_t arg1, uint32_t arg2);
static void slot_call_driver_with_r9_4(void* func_ptr, void* r9_target_ram, uint32_t arg0, uint32_t arg1, uint32_t arg2,
                                       uint32_t arg3);

/*
 * Helper for calling driver->event(int32_t id, int64_t value) with the
 * correct AAPCS register placement: id in r0, value in r2:r3, r1 unused.
 */

static inline void slot_call_driver_event(void* func_ptr, void* r9_target_ram,
                                          int32_t id, int64_t value)
{
    slot_call_driver_with_r9_4(func_ptr, r9_target_ram,
                               (uint32_t)id,
                               0u, /* r1 padding */
                               (uint32_t)(value & 0xFFFFFFFFu),
                               (uint32_t)(value >> 32));
}

// Macro to grab the device pointer instead of the raw I2C spec
#define EEPROM_DEVICE_GET(n) DEVICE_DT_GET(DT_NODELABEL(eeprom##n)),

// Array of EEPROM device pointers managed by the Zephyr driver
static const struct device* const eeprom_devices[] = {
    EEPROM_DEVICE_GET(0)
    EEPROM_DEVICE_GET(1)
    EEPROM_DEVICE_GET(2)
    EEPROM_DEVICE_GET(3)
    EEPROM_DEVICE_GET(4)
    EEPROM_DEVICE_GET(5)
    EEPROM_DEVICE_GET(6)
    EEPROM_DEVICE_GET(7)
};

static const struct gpio_dt_spec pwr_pins[] = {
    DT_FOREACH_CHILD_STATUS_OKAY(PWR_PARENT, GPIO_SPEC_FROM_CHILD)
};

static const struct gpio_dt_spec rst_pins[] = {
    DT_FOREACH_CHILD_STATUS_OKAY(RST_PARENT, GPIO_SPEC_FROM_CHILD)
};

/*
 * Serializes access to the shared slot resources (I2C mux, SPI, EEPROMs) and
 * the current slot selection. The I/O thread holds this while ticking a slot's
 * PIC driver; the management thread must hold it while reading/writing a slot
 * EEPROM so the two never fight over the bus or the selected slot.
 */
static K_MUTEX_DEFINE(slot_bus_mutex);

/*
 * Settle time given to the TCA9548A analog switch after a channel change,
 * before the first EEPROM clock edge, so the freshly-connected SDA/SCL lines
 * are stable and we don't clip the leading SCL pulse into the M24M01.
 */
#define SLOT_MUX_SETTLE_US 50

/* TCA9548A I2C control-slave address on the root (host-side) i2c1 bus. */
#define TCA9548_ADDR 0x70

static bool initialized;
static int selected_slot;
static uint32_t powered = 0;
static uint32_t reset_asserted = 0;

static eeprom_layout_t eeprom_buffer;
static uint8_t text_area[1024]; // 0x0200
static uint8_t pic_arm[2048]; // 0x1000

static slot_info_t slot_info[MAX_SLOTS];

static int slot_set_state(const struct gpio_dt_spec* pins, size_t pin_count, uint8_t slot, bool active)
{
    if (slot >= pin_count)
    {
        return -EINVAL;
    }

    return gpio_pin_set_dt(&pins[slot], active ? 1 : 0);
}

static void copy(const uint32_t* src, char** dest)
{
    char* ptr = (char*)*src;
    size_t len = *(src + 1);

    // ReSharper disable once CppDFAMemoryLeak
    char* value = malloc(len);
    memcpy(value, ptr, len);
    dest[len] = 0;
}

static int power_on()
{
    for (size_t i = 0; i <= slot_count(); i++)
    {
        if (!gpio_is_ready_dt(&pwr_pins[i]))
        {
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(&pwr_pins[i], GPIO_OUTPUT_ACTIVE);
        if (ret < 0)
        {
            return ret;
        }
        powered |= (1 << i);
    }
    return 0;
}

static int deactive_reset(void)
{
    for (size_t i = 0; i <= slot_count(); i++)
    {
        if (!gpio_is_ready_dt(&rst_pins[i]))
        {
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(&rst_pins[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0)
        {
            return ret;
        }
        reset_asserted &= ~(1 << i);
    }
    return 0;
}

static int read_eeprom(const struct device* eeprom, off_t address_in_eeprom, size_t size, void* output)
{
    return eeprom_read(eeprom, address_in_eeprom, output, size);
}

static uint32_t read_eeprom_fingerprint(const struct device* eeprom)
{
    uint32_t fingerprint;
    int result = read_eeprom(eeprom, 0, 4, &fingerprint);
    if (result)
        return 0;
    return fingerprint;
}

static int read_eeprom_metadata(const struct device* eeprom)
{
    size_t length = (void*)&eeprom_buffer.reserved3 - (void*)&eeprom_buffer;
    return read_eeprom(eeprom, 4, length, (uint8_t*)&eeprom_buffer.serial_number);
}

static int read_eeprom_textarea(const struct device* eeprom)
{
    return read_eeprom(eeprom, 0x0200, sizeof(text_area), text_area);
}

static int read_eeprom_code(const struct device* eeprom)
{
    return read_eeprom(eeprom, 0x1000, sizeof(pic_arm), pic_arm);
}

static void driver_init(slot_info_t* slot)
{
    // Call the driver's initialize() life-cycle function.
    if (slot->vmt && slot->vmt->initialize)
    {
        slot_call_driver_with_r9((void*)slot->vmt->initialize, slot->driver_ram, 0, 0);
    }
}

static void driver_event(slot_info_t* slot, int32_t event_id, int64_t value)
{
    if (slot->vmt && slot->vmt->event)
    {
        slot_call_driver_event((void*)slot->vmt->event, slot->driver_ram, event_id, value);
    }
}

int slots_initialize()
{
    if (initialized)
        return 0;
    int result = power_on();
    if (result)
        return result;
    result = deactive_reset();
    if (result)
        return result;
    k_msleep(20); // Allow for hardware to wake up. Especially boards with MCUs.
    for (size_t i = 1; i <= slot_count(); i++)
    {
        slot_select(i);
        const struct device* eeprom = eeprom_devices[i];
        uint32_t fingerprint = read_eeprom_fingerprint(eeprom);
        if (fingerprint == 0xdeadface)
        {
            result = read_eeprom_metadata(eeprom);
            if (!result)
            {
                copy(&eeprom_buffer.vendor_name_ptr, &slot_info[i].vendor);
                copy(&eeprom_buffer.vendor_model_ptr, &slot_info[i].model);
                memset(&slot_info[i].revision, 0, 5);
                memcpy(&slot_info[i].revision[0], &eeprom_buffer.vendor_revision, 4);
                copy(&eeprom_buffer.doc_link_ptr, &slot_info[i].doc_link);
                copy(&eeprom_buffer.product_link_ptr, &slot_info[i].product_link);

                result = read_eeprom_textarea(eeprom);
                if (!result)
                {
                    result = read_eeprom_code(eeprom);
                    if (!result)
                    {
                        memcpy(&slot_info[i].code_pic, &pic_arm, eeprom_buffer.code_pic_len);
                        // found = true;
                    }
                }
            }
        }
    }
    for (int i = 0; i <= slot_count(); i++)
    {
        slot_select(i);
        slot_info_t* slot = &slot_info[i];
        if (slot->vendor == NULL || slot->model == NULL || slot->code_pic[0] == 0)
        {
            // Then there is either no I/O module mounted, or the I/O module is not initialized from factory.
            continue;
        }
        uint32_t code_base = (uint32_t)&slot->code_pic[0];
        uint32_t thumb_entry_address = code_base | 0x1u;

        // 1. Call module_entry() at code_pic[0] with R9 = slot's RAM.
        //    R0 = &host_api on entry; returns Driver_Interface_t* in R0.
        uint32_t ret = slot_call_driver_with_r9_ret(
            (void*)thumb_entry_address,
            slot->driver_ram,
            (uint32_t)&host_api,
            0);

        if (ret == 0)
        {
            slot->vmt = NULL;
            continue;
        }

        // The vmt returned by module_entry() lives inside code_pic. Its
        // function-pointer fields are stored as code_pic-relative offsets
        // (already with the Thumb bit set, e.g. 0x1d, 0x21, ...). Rebase
        // them to absolute addresses so we can BLX them directly.
        Driver_Interface_t* vmt = (Driver_Interface_t*)ret;
        uint32_t* slots_arr = (uint32_t*)vmt;
        const size_t n = sizeof(Driver_Interface_t) / sizeof(uint32_t);
        for (size_t k = 0; k < n; k++)
        {
            if (slots_arr[k] != 0)
            {
                slots_arr[k] += code_base;
            }
        }
        slot->vmt = vmt;

        driver_init(slot);
    }
    for (int i = 0; i <= slot_count(); i++)
    {
        printk("Slot %d: %s %s, Revision: %s, Documentation: %s, Product Page: %s\n",
               i,
               slot_info[i].vendor,
               slot_info[i].model,
               slot_info[i].revision,
               slot_info[i].doc_link,
               slot_info[i].product_link);
    }
    return 0;
}

void slots_tick(uint8_t slot_number, int64_t now)
{
    k_mutex_lock(&slot_bus_mutex, K_FOREVER);

    // Select and drive the slot inside the lock: slot_select() only records the
    // target (the mux routes on actual bus access), so it must be atomic with
    // the driver's bus traffic against any management-thread EEPROM access.
    slot_select(slot_number);
    int32_t event_id = create_user_event(COLIBRI_EVENT_TYPE_TIME_PERIOD, 0);
    slot_info_t* slot = &slot_info[slot_number];
    driver_event(slot, event_id, now);

    k_mutex_unlock(&slot_bus_mutex);
}

/*
 * Borrow a slot's EEPROM for out-of-band access (e.g. the management thread
 * uploading/reading a PIC image). Blocks until the I/O thread is between ticks,
 * then selects the slot and hands back its EEPROM device. The caller MUST call
 * slots_eeprom_release() when done, and must not tick in the meantime.
 *
 * Returns NULL (without taking the lock) if the slot is out of range or its
 * EEPROM device is not ready.
 */
const struct device* slot_acquire(uint8_t slot)
{
    if (slot >= ARRAY_SIZE(eeprom_devices))
    {
        return NULL;
    }

    const struct device* eeprom = eeprom_devices[slot];
    if (!device_is_ready(eeprom))
    {
        return NULL;
    }

    k_mutex_lock(&slot_bus_mutex, K_FOREVER);
    slot_select(slot);
    return eeprom;
}

void slot_release(void)
{
    k_mutex_unlock(&slot_bus_mutex);
}

int slot_set_power_state(uint8_t slot, bool enabled)
{
    if (!enabled)
        powered &= ~(1 << slot);
    int result = slot_set_state(pwr_pins, ARRAY_SIZE(pwr_pins), slot, enabled);
    if (enabled)
        powered |= (1 << slot);
    return result;
}

int slot_set_reset_state(uint8_t slot, bool asserted)
{
    int result = slot_set_state(rst_pins, ARRAY_SIZE(rst_pins), slot, asserted);
    if (result == 0)
    {
        if (asserted)
        {
            reset_asserted |= 1 << slot;
        }
        else
        {
            reset_asserted &= ~(1 << slot);
        }
    }
    return result;
}

bool slot_is_powered(uint8_t slot)
{
    return powered & 1 << slot;
}

bool slot_is_reset_asserted(uint8_t slot)
{
    return (reset_asserted & (1 << slot)) != 0;
}

void slot_select(uint8_t slot)
{
    // Setting the I2C MUX and setting the SPI CS pins is NOT NEEDED thanks to the Zephyr device tree.
    // it sets all of that when we access the I2C/SPI devices on the I/O modules.

    // BUT SKIP if power is off or reset is active
    if (slot_is_reset_asserted(slot) || !slot_is_powered(slot))
    {
        return;
    }
    selected_slot = slot;
}

int slot_selected()
{
    return selected_slot;
}


static void slot_call_driver_with_r9(void* func_ptr, void* r9_target_ram, uint32_t arg1, uint32_t arg2)
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
static uint32_t slot_call_driver_with_r9_ret(void* func_ptr, void* r9_target_ram, uint32_t arg1, uint32_t arg2)
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
static void slot_call_driver_with_r9_4(void* func_ptr, void* r9_target_ram,
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
