#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "colibri-sdk/colibri-events.h"
#include "colibri/management.h"
#include "colibri/slots.h"

/*
 * Manufacturing test/calibration access to the event bus: publish an I/O
 * event to a slot, or block waiting for one. Used to drive and read back
 * I/O modules from the desktop without a Lua script loaded. Not used in
 * the field, so there is no persistent subscription state -- "event"
 * subscribes, waits up to a second, and unsubscribes again within the
 * single request/response.
 */

#define IO_TEST_SLOT_MAX (MAX_SLOTS - 1)
#define IO_TEST_WAIT_TIMEOUT K_MSEC(1000)

static int io_test_handler_publish(struct smp_streamer* ctxt);
static int io_test_handler_wait_event(struct smp_streamer* ctxt);
static int io_test_handler_rescan(struct smp_streamer* ctxt);

static const struct mgmt_handler io_test_handlers[] = {
    [0] = {.mh_read = NULL, .mh_write = io_test_handler_publish},
    [1] = {.mh_read = io_test_handler_wait_event, .mh_write = NULL},
    [2] = {.mh_read = NULL, .mh_write = io_test_handler_rescan},
};

static struct mgmt_group io_test_group = {
    .mg_handlers = io_test_handlers,
    .mg_handlers_count = ARRAY_SIZE(io_test_handlers),
    .mg_group_id = IO_TEST_MANAGEMENT_GROUP_ID,
};

void io_test_management_initialize(void)
{
    mgmt_register_group(&io_test_group);
}

static bool decode_slot_type_param(zcbor_state_t* zsd, int64_t* slot, int64_t* type, int64_t* param)
{
    size_t decoded_fields_count;

    struct zcbor_map_decode_key_val decode[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("slot", zcbor_int64_decode, slot),
        ZCBOR_MAP_DECODE_KEY_DECODER("type", zcbor_int64_decode, type),
        ZCBOR_MAP_DECODE_KEY_DECODER("param", zcbor_int64_decode, param),
    };

    bool ok = zcbor_map_decode_bulk(zsd, decode, ARRAY_SIZE(decode), &decoded_fields_count) == 0;

    return ok && *slot >= 0 && *slot <= IO_TEST_SLOT_MAX && *type >= 0 && *param >= 0 && *param <= UINT16_MAX;
}

static int io_test_handler_publish(struct smp_streamer* ctxt)
{
    zcbor_state_t* zsd = ctxt->reader->zs; // Input state
    zcbor_state_t* zse = ctxt->writer->zs; // Output state

    int64_t slot = -1;
    int64_t type = -1;
    int64_t param = -1;
    int64_t value = 0;
    size_t decoded_fields_count;
    bool ok;

    struct zcbor_map_decode_key_val decode[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("slot", zcbor_int64_decode, &slot),
        ZCBOR_MAP_DECODE_KEY_DECODER("type", zcbor_int64_decode, &type),
        ZCBOR_MAP_DECODE_KEY_DECODER("param", zcbor_int64_decode, &param),
        ZCBOR_MAP_DECODE_KEY_DECODER("value", zcbor_int64_decode, &value),
    };
    ok = zcbor_map_decode_bulk(zsd, decode, ARRAY_SIZE(decode), &decoded_fields_count) == 0;

    if (!ok || slot < 0 || slot > IO_TEST_SLOT_MAX || type < 0 || param < 0 || param > UINT16_MAX)
    {
        return MGMT_ERR_EINVAL;
    }

    event_t event = (event_t) {.slot=slot, .type=type,.parameter=param, .io=true};
    events_publish(event, value);

    ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, 0);

    return ok ? 0 : MGMT_ERR_ENOMEM;
}

typedef struct
{
    struct k_sem sem;
    int64_t value;
} wait_ctx_t;

static void wait_callback(event_t event, int64_t value, int32_t user_data)
{
    ARG_UNUSED(event);
    wait_ctx_t* ctx = (wait_ctx_t*)(intptr_t)user_data;
    ctx->value = value;
    k_sem_give(&ctx->sem);
}

static int io_test_handler_wait_event(struct smp_streamer* ctxt)
{
    zcbor_state_t* zsd = ctxt->reader->zs; // Input state
    zcbor_state_t* zse = ctxt->writer->zs; // Output state

    int64_t slot;
    int64_t type;
    int64_t param;

    if (!decode_slot_type_param(zsd, &slot, &type, &param))
    {
        return MGMT_ERR_EINVAL;
    }

    event_t event = (event_t) {.slot=slot, .type=type,.parameter=param, .io=true};

    wait_ctx_t ctx = {.value = 0};
    k_sem_init(&ctx.sem, 0, 1);

    void* sub = events_subscribe(event, wait_callback, (int32_t)(intptr_t)&ctx);
    if (sub == (void*)-1)
    {
        return MGMT_ERR_ENOMEM;
    }

    int rc = k_sem_take(&ctx.sem, IO_TEST_WAIT_TIMEOUT);
    events_unsubscribe(sub);

    if (rc != 0)
    {
        return zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, -ETIMEDOUT)
            ? 0 : MGMT_ERR_ENOMEM;
    }

    bool ok = zcbor_tstr_put_lit(zse, "rc")
        && zcbor_int32_put(zse, 0)
        && zcbor_tstr_put_lit(zse, "value")
        && zcbor_int64_put(zse, ctx.value);

    return ok ? 0 : MGMT_ERR_ENOMEM;
}

/*
 * Re-enumerate and reload the driver for a single slot -- used after the
 * operator swaps in a new module on the manufacturing bench, since
 * slots_initialize() only ever runs once, at boot.
 */
static int io_test_handler_rescan(struct smp_streamer* ctxt)
{
    zcbor_state_t* zsd = ctxt->reader->zs; // Input state
    zcbor_state_t* zse = ctxt->writer->zs; // Output state

    int64_t slot = -1;
    size_t decoded_fields_count;

    struct zcbor_map_decode_key_val decode[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("slot", zcbor_int64_decode, &slot),
    };
    bool ok = zcbor_map_decode_bulk(zsd, decode, ARRAY_SIZE(decode), &decoded_fields_count) == 0;

    if (!ok || slot < 0 || slot > IO_TEST_SLOT_MAX)
    {
        return MGMT_ERR_EINVAL;
    }

    int rc = slots_reinit_one((uint8_t)slot);

    ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, rc);

    return ok ? 0 : MGMT_ERR_ENOMEM;
}
