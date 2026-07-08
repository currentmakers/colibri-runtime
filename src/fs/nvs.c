#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>

/* Use the node label directly */
#define SETTINGS_NODE DT_NODELABEL(settings_carrier_partition)

static struct nvs_fs fs;

int nvs_initialize() {
    int rc;
    struct flash_pages_info info;
    const struct device *flash_dev;

    flash_dev = DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(SETTINGS_NODE));
    if (!device_is_ready(flash_dev)) {
        return -ENODEV;
    }

    fs.flash_device = flash_dev;
    fs.offset = DT_REG_ADDR(SETTINGS_NODE);
    rc = flash_get_page_info_by_offs(flash_dev, fs.offset, &info);
    if (rc) {
        return rc;
    }
    fs.sector_size = info.size;
    fs.sector_count = DT_REG_SIZE(SETTINGS_NODE) / info.size;

    rc = nvs_mount(&fs);
    if (rc == 0) {
        printk("NVS initialized successfully!\n");
    }
    return rc;
}