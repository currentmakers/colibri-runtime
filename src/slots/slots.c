#include "colibri-sdk/colibri.h"
#include "colibri-sdk/colibri-io-eeprom.h"
#include "colibri/code-arena.h"
#include "colibri/host-api.h"
#include "colibri/slots.h"

#include <ctype.h>
#include <stdlib.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

#include "colibri/i2c.h"
#include "colibri/supervisor.h"

#define PWR_PARENT DT_NODELABEL(slot_ctrl_pwr)
#define RST_PARENT DT_NODELABEL(slot_ctrl_rst)

#define GPIO_SPEC_FROM_CHILD(node_id) GPIO_DT_SPEC_GET(node_id, gpios),

static const char empty_vendor[] = "<none>";
static const char empty_model[] = "<empty slot>";
static const char empty_doc_link[] = "https://stm32world.com/wiki/Colibri";
static const char empty_prod_link[] = "https://currentmakers.com/products/colibri/";

static const char broken_vendor[] = "<unknown>";
static const char broken_model[] = "<invalid>";
static const char broken_doc_link[] = "https://stm32world.com/wiki/Colibri_Initialization_Troubleshooting";
static const char broken_prod_link[] = "";

static const uint8_t io_empty[] = {
    0x40, 0xf2, 0x08, 0x00, 0xc0, 0xf2, 0x00, 0x00, 0x78, 0x44, 0x70, 0x47,
    0x70, 0x47, 0x00, 0xbf, 0x70, 0x47, 0xd4, 0xd4, 0x0d, 0x00, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x00
};

const uint8_t io_broken[] = {
    0x01, 0x46, 0x40, 0xf2, 0x7e, 0x00, 0x40, 0xf2, 0x00, 0x02, 0xc0, 0xf2,
    0x00, 0x00, 0xc0, 0xf2, 0x00, 0x02, 0x78, 0x44, 0x49, 0xf8, 0x02, 0x10,
    0x70, 0x47, 0xd4, 0xd4, 0x70, 0x47, 0x00, 0xbf, 0xb0, 0xb5, 0x02, 0xaf,
    0x40, 0xf2, 0x08, 0x00, 0xc0, 0xf2, 0x00, 0x00, 0x09, 0xeb, 0x00, 0x01,
    0x59, 0xf8, 0x00, 0x40, 0x49, 0x68, 0xa4, 0x1a, 0x99, 0x41, 0x28, 0xbf,
    0xb0, 0xbd, 0x40, 0xf2, 0x10, 0x05, 0xc0, 0xf2, 0x00, 0x05, 0x59, 0xf8,
    0x05, 0x10, 0x64, 0x24, 0x00, 0x29, 0x8c, 0x46, 0x0e, 0xbf, 0x4f, 0xf4,
    0x61, 0x74, 0x6f, 0xf0, 0x02, 0x0c, 0x4f, 0xf0, 0xff, 0x31, 0x12, 0x19,
    0x49, 0xf8, 0x00, 0x20, 0x40, 0xf2, 0x00, 0x02, 0xc0, 0xf2, 0x00, 0x02,
    0x59, 0xf8, 0x02, 0x20, 0x43, 0xf1, 0x00, 0x03, 0x48, 0x44, 0x54, 0x68,
    0x43, 0x60, 0x4f, 0xf4, 0x88, 0x10, 0x62, 0x46, 0x0b, 0x46, 0xa0, 0x47,
    0x59, 0xf8, 0x05, 0x00, 0xb0, 0xfa, 0x80, 0xf0, 0x40, 0x09, 0x49, 0xf8,
    0x05, 0x00, 0xb0, 0xbd, 0x1d, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00
};


/*
 * Helper for calling driver->event(int32_t id, int64_t value) with the
 * correct AAPCS register placement: id in r0, value in r2:r3, r1 unused.
 */
// static inline void hostapi_call_driver_event(void* func_ptr, void* r9_target_ram,
//                                           int32_t id, int64_t value)
// {
//     slot_call_driver_with_r9_4(func_ptr, r9_target_ram,
//                                (uint32_t)id,
//                                0u, /* r1 padding */
//                                (uint32_t)(value & 0xFFFFFFFFu),
//                                (uint32_t)(value >> 32));
// }

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
 *
 * UPDATE: According to both documentation and Gemini (Pro), it should not be
 *         needed to do manual synchronization. Zephyr is supposed to handle i
 *         and if there are problems, then we are looking at bugs. Timing is
 *         quoted by Gemini as a concern, but we are not nearly timing critical.
 *         HOWEVER, there might be issues if reading and writing to the same
 *         device at the same time. I am not sure that this can ever happen,
 *         as the EEPROMs are cached in RAM after boot.
 */
// static K_MUTEX_DEFINE(slot_bus_mutex);

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

static slot_info_t slot_info[MAX_SLOTS] __dtcm_noinit_section __aligned(8);

static int slot_set_state(const struct gpio_dt_spec* pins, size_t pin_count, uint8_t slot, bool active)
{
    if (slot >= pin_count)
    {
        return -EINVAL;
    }

    return gpio_pin_set_dt(&pins[slot], active ? 1 : 0);
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

static int deactivate_reset(void)
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

/*
 * Reads one 32-bit metadata field. A failed read yields 0, which for every
 * field we consume is indistinguishable from "not programmed" anyway.
 */
static uint32_t read_eeprom_u32(const struct device* eeprom, off_t offset)
{
    uint32_t value;
    if (read_eeprom(eeprom, offset, sizeof(value), &value))
        return 0;
    return value;
}

/*
 * Reads one of the length-prefixed text fields into a freshly allocated,
 * NUL-terminated buffer. The EEPROM stores the text without a terminator, so
 * the buffer is one byte longer than the stored length. Returns NULL if the
 * field is absent, heap is exhausted implausibly long or unreadable, leaving the caller's
 * existing value untouched.
 */
static char* read_eeprom_string(const struct device* eeprom, off_t offset, size_t length)
{
    if (offset == 0 || length == 0 || length > sizeof(((eeprom_layout_t*)0)->text_area))
        return NULL;

    char* buffer = malloc(length + 1);
    if (buffer == NULL)
        return NULL;

    if (read_eeprom(eeprom, offset, length, buffer))
    {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

static int read_eeprom_code(const struct device* eeprom, uint8_t slot)
{
    /*
     * The arena window is at least as large as pic_arm (asserted in
     * code-arena.c), so this never truncates the driver.
     */

    const size_t size = MIN(SLOT_CODE_SIZE, sizeof(((eeprom_layout_t*)0)->pic_arm));
    const size_t fragment_size = 512;
    off_t offset = offsetof(eeprom_layout_t, pic_arm);
    uint8_t* dest = slot_code[slot];

    for (size_t bytes_read = 0; bytes_read < size; bytes_read += fragment_size)
    {
        size_t chunk = MIN(fragment_size, size - bytes_read);
        int ret = read_eeprom(eeprom, offset + bytes_read, chunk, dest + bytes_read);
        if (ret != 0)
        {
            return ret;
        }
    }

    return 0;
}

/* vendor_revision holds up to four NUL-padded ASCII characters. */
static void revision_string(uint32_t revision, char out[5])
{
    memcpy(out, &revision, 4);
    out[4] = '\0';
    for (int i = 0; i < 4; i++)
    {
        if (out[i] != '\0' && !isprint((unsigned char)out[i]))
            out[i] = '?';
    }
}

static const char* or_unknown(const char* value)
{
    return value != NULL ? value : "<unknown>";
}

static void driver_init(slot_info_t* slot)
{
    // Call the driver's initialize() life-cycle function.
    if (slot->vmt && slot->vmt->initialize)
    {
        hostapi_call_driver_with_r9((void*)slot->vmt->initialize, slot->driver_ram, slot_selected(), (uint32_t)slot->calibration_data);
    }
}

static void driver_event(slot_info_t* slot, event_t event_id, int64_t value)
{
    if (slot->vmt && slot->vmt->event)
    {
        void* func_ptr = (void*)slot->vmt->event;
        void* r9_target_ram = slot->driver_ram;
        slot_call_driver_with_r9_4(func_ptr, r9_target_ram,
                                   event_id.value,
                                   0u, /* r1 padding */
                                   (uint32_t)(value & 0xFFFFFFFFu),
                                   (uint32_t)(value >> 32));
    }
}

/*
 * Enumerate one slot's EEPROM metadata and (re)load its driver into the
 * slot's code arena window. Shared by slots_initialize() (looped over every
 * slot at boot) and slots_reinit_one() (a single slot, e.g. after a
 * manufacturing module swap).
 *
 * Frees any vendor_name/model_name/doc_link/product_link strings left over
 * from a previous load before overwriting them -- read_eeprom_string()
 * mallocs a fresh buffer every call, and unlike at boot this can now run
 * more than once per slot. Also clears vmt up front so a slot that fails to
 * (re)load a driver never keeps pointing at a previous module's code.
 */
static void slot_enumerate_and_load(uint8_t slot_number)
{
    slot_select(slot_number);
    slot_info_t* slot = &slot_info[slot_number];

    if (slot->vendor_name != empty_vendor && slot->vendor_name != broken_vendor)
        free(slot->vendor_name);
    if (slot->model_name != empty_model && slot->model_name != broken_model)
        free(slot->model_name);
    if (slot->doc_link != empty_doc_link && slot->doc_link != broken_doc_link)
        free(slot->doc_link);
    if (slot->product_link != empty_prod_link && slot->product_link != broken_prod_link)
        free(slot->product_link);
    slot->vendor_name = NULL;
    slot->model_name = NULL;
    slot->doc_link = NULL;
    slot->product_link = NULL;
    slot->vmt = NULL;

    memset(slot_code[slot_number], 0, SLOT_CODE_SIZE);

    const struct device* eeprom = eeprom_devices[slot_number];
    uint32_t fingerprint = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, fingerprint));
    if (fingerprint == 0xdeadface)
    {
        slot->serial_number = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, serial_number));
        slot->vendor_id = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, vendor_id));
        slot->vendor_model_id = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, vendor_model_id));
        slot->revision = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, vendor_revision));

        off_t vendor_name_ptr = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, vendor_name_ptr));
        uint32_t vendor_name_len = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, vendor_name_len));
        off_t vendor_model_ptr = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, vendor_model_ptr));
        uint32_t vendor_model_len = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, vendor_model_len));
        off_t doc_ptr = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, doc_link_ptr));
        uint32_t doc_len = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, doc_link_len));
        off_t product_ptr = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, product_link_ptr));
        uint32_t product_len = read_eeprom_u32(eeprom, offsetof(eeprom_layout_t, product_link_len));

        char* data = read_eeprom_string(eeprom, vendor_name_ptr, vendor_name_len);
        if (data)
            slot->vendor_name = data;
        data = read_eeprom_string(eeprom, vendor_model_ptr, vendor_model_len);
        if (data)
            slot->model_name = data;
        data = read_eeprom_string(eeprom, doc_ptr, doc_len);
        if (data)
            slot->doc_link = data;
        data = read_eeprom_string(eeprom, product_ptr, product_len);
        if (data)
            slot->product_link = data;

        size_t size = sizeof(((eeprom_layout_t*)0)->calibration_data);
        int error = read_eeprom(eeprom, offsetof(eeprom_layout_t, calibration_data), size, slot->calibration_data);
        if (error)
        {
            printk("unable to read calibration data for slot %d\n", slot_number);
        }
        error = read_eeprom_code(eeprom, slot_number);
        if (error)
        {
            printk("Unable to read code section in slot %d\n", slot_number);
            /* Leave the window zeroed so the loader below skips this slot. */
            memset(slot_code[slot_number], 0, SLOT_CODE_SIZE);
        }
    }
    else
    {
        if (fingerprint == 0)
        {
            // Slot is empty or hardware problem
            slot->vendor_name = (char*)empty_vendor;
            slot->model_name = (char*)empty_model;
            slot->doc_link = (char*)empty_doc_link;
            slot->product_link = (char*)empty_prod_link;
            slot->revision = 'A';
            memcpy(slot_code[slot_number], io_empty, sizeof(io_empty));
        }
        else
        {
            // I/O module not initialized at factory
            slot->vendor_name = (char*)broken_vendor;
            slot->model_name = (char*)broken_model;
            slot->doc_link = (char*)broken_doc_link;
            slot->product_link = (char*)broken_prod_link;
            slot->revision = 'A';
            memcpy(slot_code[slot_number], io_broken, sizeof(io_broken));
        }
    }

    if (!(slot->vendor_name == NULL || slot->model_name == NULL || slot_code[slot_number][0] == 0))
    {
        // Otherwise there is either no I/O module mounted, or the I/O module is not initialized from factory.
        uint32_t code_base = (uint32_t)&slot_code[slot_number][0];
        uint32_t thumb_entry_address = code_base | 0x1u;

        /*
         * The driver bytes were just written as data. Drain the write buffer and
         * flush the pipeline before branching into them, so the instruction fetch
         * cannot see stale memory.
         */
        barrier_dsync_fence_full();
        barrier_isync_fence_full();

        // 1. Call module_entry() at code_pic[0] with R9 = slot's RAM.
        //    R0 = &host_api on entry; returns Driver_Interface_t* in R0.
        uint32_t ret = slot_call_driver_with_r9_ret(
            (void*)thumb_entry_address,
            slot->driver_ram,
            (uint32_t)&host_api,
            0);

        if (ret != 0)
        {
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
    }

    char revision[5];
    revision_string(slot->revision, revision);
    printk("Slot %d: %s[%u] %s, Revision: %s, Documentation: %s, Product Page: %s\n",
           slot_number,
           or_unknown(slot->vendor_name),
           slot->vendor_id,
           or_unknown(slot->model_name),
           revision,
           or_unknown(slot->doc_link),
           or_unknown(slot->product_link));
    i2c_detect();
}

static void print_text(char* text, int width)
{
    size_t len = strlen(text);
    size_t left = (width - len) / 2;
    size_t right = width - len - left;
    printk(" |%*s%s%*s|", left, "", text, right, "");
}

static void print_revision(char* text, int width)
{
    width = width - 4;
    size_t len = strlen(text);
    size_t left = (width - len) / 2;
    size_t right = width - len - left;
    printk(" |%*sRev %s%*s|", left, "", text, right, "");
}

int slots_initialize()
{
    if (initialized)
        return 0;
    int result = power_on();
    if (result)
        return result;
    result = deactivate_reset();
    if (result)
        return result;
    k_msleep(20); // Allow for hardware to wake up. Especially boards with MCUs.

    /*
     * The arena is a (NOLOAD) devicetree memory-region, so unlike .bss it is not
     * zeroed at boot and still holds whatever survived the last reset. Clear it
     * so the "slot_code[i][0] == 0" emptiness test below means what it says.
     */
    memset(slot_code, 0, sizeof(slot_code));
    memset(slot_info, 0, sizeof(slot_info));
    for (size_t i = 0; i <= slot_count(); i++)
    {
        slot_enumerate_and_load(i);
    }
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
        printk(" +---------------+");
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
        printk(" |               |");
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
    {
        print_text(slot_info[i].vendor_name, 15 );
    }
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
        printk(" |               |");
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
    {
        print_text(slot_info[i].model_name, 15 );
    }
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
    {
        print_revision((char *) &slot_info[i].revision, 15 );
    }
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
        printk(" |               |");
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
        printk(" |               |");
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
        printk(" |               |");
    printk("\n");
    for (size_t i = 1; i <= slot_count(); i++)
        printk(" +---------------+");
    printk("\n");
    return 0;
}

/*
 * Re-enumerate and reload the driver for a single slot, e.g. after a
 * manufacturing-bench module swap. Unlike slots_initialize() this can run
 * any number of times: it power-cycles just the target slot for a clean
 * power-on-reset (the rail may already have been live when the module was
 * plugged in), then re-runs the same enumeration/driver-load logic used at
 * boot for that one slot.
 */
int slots_reinit_one(uint8_t slot)
{
    if (slot > slot_count())
        return -EINVAL;

    slot_set_reset_state(slot, true);
    slot_set_power_state(slot, false);
    k_msleep(50); // Let the module fully de-energize before restoring power.
    slot_set_power_state(slot, true);
    k_msleep(20); // Same settle time as slots_initialize() gives at boot.
    slot_set_reset_state(slot, false);

    slot_enumerate_and_load(slot);
    return 0;
}

void slots_tick(uint8_t slot_number, int64_t now)
{
    event_t event_id = (event_t){.io = false, .slot = slot_number, .type = COLIBRI_EVENT_TYPE_TIME_PERIOD, .parameter = 10};
    slots_event_for(slot_number, event_id, now);
}

void slots_event_for(uint8_t slot_number, event_t event, int64_t value)
{
    supervisor_timing_start(SUPERVISOR_TIMING_BASE + slot_number);
    // k_mutex_lock(&slot_bus_mutex, K_FOREVER);

    // Select and drive the slot inside the lock: slot_select() only records the
    // target (the mux routes on actual bus access), so it must be atomic with
    // the driver's bus traffic against any management-thread EEPROM access.
    slot_select(slot_number);
    slot_info_t* slot = &slot_info[slot_number];
    driver_event(slot, event, value);

    // k_mutex_unlock(&slot_bus_mutex);
    supervisor_timing_stop(SUPERVISOR_TIMING_BASE + slot_number);
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
const struct device* slot_acquire_eeprom(uint8_t slot)
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

    // k_mutex_lock(&slot_bus_mutex, K_FOREVER);
    slot_select(slot);
    return eeprom;
}

void slot_release(void)
{
    // k_mutex_unlock(&slot_bus_mutex);
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
