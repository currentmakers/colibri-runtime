#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include "colibri/modbus.h"

int modbus_check(void) {
    const char *iface_name = "modbus0";
    int iface = modbus_iface_get_by_name(iface_name);

    if (iface < 0) {
        printk("Modbus interface %s not found!\n", iface_name);
        return -1;
    }
    printk("Modbus interface %s is ready for init.\n", iface_name);
    return 0;
}

static int read_coil(uint16_t reg, bool *result)
{
    return 0; // no error
}

static int write_coil(uint16_t reg, bool value)
{
    return 0; // no error
}

static int read_discrete(uint16_t reg, bool *result)
{
    return 0; // no error
}

static int read_input(uint16_t reg, uint16_t *result)
{
    return 0; // no error
}

static int read_float_input(uint16_t reg, float *result)
{
    return 0; // no error
}

static int read_holding(uint16_t reg, uint16_t *result)
{
    return 0; // no error
}

static int write_holding(uint16_t reg, uint16_t value)
{
    return 0; // no error
}

static int read_float_holding(uint16_t reg, float *result)
{
    return 0; // no error
}

static int write_float_holding(uint16_t reg, float value)
{
    return 0; // no error
}

static struct modbus_user_callbacks mbs_cbs = {
    /** Coil read callback */
    .coil_rd = read_coil,

    /** Coil write callback */
    .coil_wr = write_coil,

    /** Discrete Input read callback */
    .discrete_input_rd = read_discrete,

    /** Input Register read callback */
    .input_reg_rd = read_input,

    /** Floating Point Input Register read callback */
    .input_reg_rd_fp = read_float_input,

    /** Holding Register read callback */
    .holding_reg_rd = read_holding,

    /** Holding Register write callback */
    .holding_reg_wr = write_holding,

    /** Floating Point Holding Register read callback */
    .holding_reg_rd_fp = read_float_holding,

    /** Floating Point Holding Register write callback */
    .holding_reg_wr_fp = write_float_holding
};

int modbus_initialize(struct modbus_user_callbacks *callbacks) {

    int iface = modbus_iface_get_by_name("modbus0");
    const static struct modbus_iface_param server_param = {
        .mode = MODBUS_MODE_RTU,
        .server = {
            .user_cb = &mbs_cbs,
            .unit_id = 1,
        },
        .serial = {
            .baud = 19200,
            .parity = UART_CFG_PARITY_NONE,
        },
    };

    int err = modbus_init_server(iface, server_param);

    if (err) {
        printk("Modbus init failed: %d\n", err);
    }
    return err;
}
