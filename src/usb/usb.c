#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>

int usb_initialize()
{
    uint32_t dtr = 0;

    int error = usb_enable(NULL);
    if (error != 0) {
        printk("Failed to enable USB stack. Aborting boot.\n");
        return error;
    }

    /* Locate the virtual serial device defined in your devicetree */
    const struct device* usb_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_mcumgr));
    if (!device_is_ready(usb_dev)) {
        printk("USB CDC ACM device not ready\n");
        return -ENODEV;
    }

    // printk("Waiting for USB host connection...\n");
    // while (!dtr) {
    //     uart_line_ctrl_get(usb_dev, UART_LINE_CTRL_DTR, &dtr);
    //     k_sleep(K_MSEC(100));
    // }
    printk("USB Host connected. Initializing MCUmgr line processing.\n");
    return 0;
}