#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "colibri/i2c.h"
#include "colibri/management.h"
#include "colibri/slots.h"

#define MGMT_ERR_EEPROM_READ (MGMT_ERR_EPERUSER + 0)
#define MGMT_ERR_EEPROM_WRITE (MGMT_ERR_EPERUSER + 1)

/*
 * Maximum bytes transferred per request. A whole SMP frame must fit inside the
 * UART transport MTU (CONFIG_MCUMGR_TRANSPORT_UART_MTU, 256 by default), so we
 * cap a single read/write window well below that. Larger transfers (up to the
 * 8 kB EEPROM) are done by the client issuing successive addr/n windows.
 */
#define EEPROM_XFER_MAX 128

/* slot field in an event is 5 bits, and there are MAX_SLOTS physical slots. */
#define EEPROM_SLOT_MAX (MAX_SLOTS - 1)

static int eeprom_handler_read(struct smp_streamer* ctxt);
static int eeprom_handler_write(struct smp_streamer* ctxt);
extern void fs_upload_mkdir_initialize();

/* command handlers */
static const struct mgmt_handler eeprom_handlers[] = {
    [0] = {.mh_read = eeprom_handler_read, .mh_write = NULL},
    [1] = {.mh_read = NULL, .mh_write = eeprom_handler_write},
};

static struct mgmt_group eeprom_group = {
    .mg_handlers = eeprom_handlers,
    .mg_handlers_count = ARRAY_SIZE(eeprom_handlers),
    .mg_group_id = EEPROM_MANAGEMENT_GROUP_ID,
};

int management_initialize()
{
    mgmt_register_group(&eeprom_group);
    fs_upload_mkdir_initialize();
    return 0;
}

static int eeprom_handler_read(struct smp_streamer* ctxt)
{
    zcbor_state_t* zsd = ctxt->reader->zs; // Input state
    zcbor_state_t* zse = ctxt->writer->zs; // Output state

    int64_t slot = -1;
    int64_t addr = -1;
    int64_t n = 0;
    size_t decoded_fields_count;
    bool ok;

    // Define the parsing schema for incoming keys
    struct zcbor_map_decode_key_val read_decode[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("slot", zcbor_int64_decode, &slot),
        ZCBOR_MAP_DECODE_KEY_DECODER("addr", zcbor_int64_decode, &addr),
        ZCBOR_MAP_DECODE_KEY_DECODER("n", zcbor_int64_decode, &n),
    };
    // Decode the whole map in one shot using Zephyr's bulk utility
    ok = zcbor_map_decode_bulk(zsd, read_decode, ARRAY_SIZE(read_decode), &decoded_fields_count) == 0;

    // Validate we got the expected arguments and bounds are safe
    if (!ok || slot < 0 || slot > EEPROM_SLOT_MAX || addr < 0 || n <= 0 || n > EEPROM_XFER_MAX)
    {
        return MGMT_ERR_EINVAL;
    }

    // Borrow the slot's EEPROM under the shared bus lock (blocks the I/O thread
    // until we release). Read the window, then release as soon as possible.
    const struct device* eeprom = slot_acquire((uint8_t)slot);
    if (eeprom == NULL)
    {
        slot_release();
        return MGMT_ERR_EINVAL;
    }

    uint8_t buffer[EEPROM_XFER_MAX];

    int rc = eeprom_read(eeprom, (off_t)addr, buffer, (size_t)n);
    slot_release();

    if (rc)
    {
        printk("Unable to read eeprom (slot %lld): %d\n", slot, rc);
        // Surface the driver errno to the client as the response "rc" rather
        // than collapsing every failure into MGMT_ERR_EEPROM_READ.
        return zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, rc)
            ? 0 : MGMT_ERR_ENOMEM;
    }

    // Wrap the binary payload into a zcbor_string structure
    struct zcbor_string response_data = {
        .value = buffer,
        .len = (size_t)n
    };

    // NOTE: the SMP framework has already opened the top-level response map, so
    // we only add key/value pairs here -- opening our own map would nest a
    // second map inside it and produce malformed { { ... } } output.
    ok = zcbor_tstr_put_lit(zse, "rc")
        && zcbor_int32_put(zse, 0);

    // Add the read data byte string ("data": h'...')
    ok = ok && zcbor_tstr_put_lit(zse, "data")
        && zcbor_bstr_encode(zse, &response_data);

    if (!ok)
    {
        return MGMT_ERR_ENOMEM;
    }

    return 0; // Success!
}

static int eeprom_handler_write(struct smp_streamer* ctxt)
{
    zcbor_state_t* zsd = ctxt->reader->zs; // Input state
    zcbor_state_t* zse = ctxt->writer->zs; // Output state

    int64_t slot = -1;
    int64_t addr = -1;
    uint8_t write_buffer[EEPROM_XFER_MAX];
    size_t write_len = 0;
    bool ok;

    // Start decoding the main map
    ok = zcbor_map_start_decode(zsd);

    // Use zcbor_array_at_end() to safely loop through map key-value pairs
    while (ok && !zcbor_array_at_end(zsd))
    {
        struct zcbor_string key;
        ok = zcbor_tstr_decode(zsd, &key);
        if (!ok) break;

        if (strncmp("slot", key.value, 4) == 0)
        {
            ok = zcbor_int64_decode(zsd, &slot);
        }
        else if (strncmp("addr", key.value, 4) == 0)
        {
            ok = zcbor_int64_decode(zsd, &addr);
        }
        else if (strncmp("values", key.value, 6) == 0)
        {
            // 2. Found the "values" array. In ZCBOR, JSON arrays are "lists".
            ok = zcbor_list_start_decode(zsd);

            // Loop through the array elements
            while (ok && !zcbor_array_at_end(zsd))
            {
                int64_t val;
                ok = zcbor_int64_decode(zsd, &val);

                if (ok)
                {
                    if (write_len < sizeof(write_buffer))
                    {
                        write_buffer[write_len++] = (uint8_t)val;
                    }
                    else
                    {
                        ok = false; // Buffer overflow protection
                    }
                }
            }
            // Close the list decoding
            ok = ok && zcbor_list_end_decode(zsd);
        }
        else
        {
            // Skip any other unexpected keys safely
            ok = zcbor_any_skip(zsd, NULL);
        }
    }
    // Close the main map decoding
    ok = ok && zcbor_map_end_decode(zsd);

    // Validate parsing success, slot/addr bounds and data presence
    if (!ok || slot < 0 || slot > EEPROM_SLOT_MAX || addr < 0 || write_len == 0)
    {
        return MGMT_ERR_EINVAL;
    }

    // Borrow the slot's EEPROM under the shared bus lock, write, then release.
    const struct device* eeprom = slot_acquire((uint8_t)slot);
    if (eeprom == NULL)
    {
        return MGMT_ERR_EINVAL;
    }

    int rc = eeprom_write(eeprom, (off_t)addr, write_buffer, write_len);
    slot_release();

    if (rc)
    {
        printk("Unable to write eeprom (slot %lld): %d\n", slot, rc);
        // Surface the driver errno to the client as the response "rc".
        return zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, rc)
            ? 0 : MGMT_ERR_ENOMEM;
    }

    // Encode the response ("rc": 0). The SMP framework already opened the
    // top-level map for us, so just add the pair -- do not open another map.
    ok = zcbor_tstr_put_lit(zse, "rc")
        && zcbor_int32_put(zse, 0);

    if (!ok)
    {
        return MGMT_ERR_ENOMEM;
    }

    return 0; // Success!
}
