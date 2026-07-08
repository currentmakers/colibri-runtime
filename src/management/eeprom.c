#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "colibri/management.h"

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

    int64_t addr = 0;
    int64_t n = 0;
    size_t decoded_fields_count;
    bool ok;

    // 1. Define the parsing schema for incoming keys
    struct zcbor_map_decode_key_val read_decode[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("addr", zcbor_int64_decode, &addr),
        ZCBOR_MAP_DECODE_KEY_DECODER("n", zcbor_int64_decode, &n),
    };

    // 2. Decode the whole map in one shot using Zephyr's bulk utility
    ok = zcbor_map_decode_bulk(zsd, read_decode, ARRAY_SIZE(read_decode), &decoded_fields_count) == 0;

    // Validate we got the expected arguments and bounds are safe
    if (!ok || n <= 0 || n > 128)
    {
        return MGMT_ERR_EINVAL;
    }

    // 3. Execute your business logic (Read from physical EEPROM)
    uint8_t buffer[128];
    // int rc = eeprom_read(eeprom_dev, (uint32_t)addr, buffer, (uint32_t)n);
    // (Assume buffer is filled with fake calibration data [0xAA, 0xBB, 0xCC...] for now)
    buffer[0] = 'N';
    buffer[1] = 'i';
    buffer[2] = 'c';
    buffer[3] = 'l';
    buffer[4] = 'a';
    buffer[5] = 's';
    buffer[6] = '\0';
    n = 7;
    // 4. Wrap the binary payload into a zcbor_string structure
    struct zcbor_string response_data = {
        .value = buffer,
        .len = (size_t)n
    };

    // 5. Encode the response payload.
    // NOTE: the SMP framework has already opened the top-level response map
    // (smp.c: zcbor_map_start_encode before the handler, map_end after), so we
    // only add key/value pairs here -- opening our own map would nest a second
    // map inside it and produce malformed { { ... } } output.

    // Add return code ("rc": 0)
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
    zcbor_state_t *zsd = ctxt->reader->zs; // Input state
    zcbor_state_t *zse = ctxt->writer->zs; // Output state

    int64_t addr = 0;
    uint8_t write_buffer[128];
    size_t write_len = 0;
    bool ok;

    // 1. Start decoding the main map
    ok = zcbor_map_start_decode(zsd);

    // Use zcbor_array_at_end() to safely loop through map key-value pairs
    while (ok && !zcbor_array_at_end(zsd)) {
        struct zcbor_string key;
        ok = zcbor_tstr_decode(zsd, &key);
        if (!ok) break;

        if (strncmp(key.value, "addr", key.len) == 0) {
            ok = zcbor_int64_decode(zsd, &addr);
        } else if (strncmp(key.value, "values", key.len) == 0) {
            // 2. Found the "values" array. In ZCBOR, JSON arrays are "lists".
            ok = zcbor_list_start_decode(zsd);

            // Loop through the array elements
            while (ok && !zcbor_array_at_end(zsd)) {
                int64_t val;
                ok = zcbor_int64_decode(zsd, &val);

                if (ok) {
                    if (write_len < sizeof(write_buffer)) {
                        write_buffer[write_len++] = (uint8_t)val;
                    } else {
                        ok = false; // Buffer overflow protection
                    }
                }
            }
            // Close the list decoding
            ok = ok && zcbor_list_end_decode(zsd);
        } else {
            // Skip any other unexpected keys safely
            ok = zcbor_any_skip(zsd, NULL);
        }
    }
    // Close the main map decoding
    ok = ok && zcbor_map_end_decode(zsd);

    // Validate parsing success and data presence
    if (!ok || write_len == 0) {
        return MGMT_ERR_EINVAL;
    }

    // 3. Execute your business logic (Write to physical EEPROM)
    // int rc = eeprom_write(eeprom_dev, (uint32_t)addr, write_buffer, write_len);

    // 4. Encode the response ("rc": 0). The SMP framework already opened the
    // top-level map for us, so just add the pair -- do not open another map.
    ok = zcbor_tstr_put_lit(zse, "rc")
            && zcbor_int32_put(zse, 0);

    if (!ok) {
        return MGMT_ERR_ENOMEM;
    }

    return 0; // Success!
}
