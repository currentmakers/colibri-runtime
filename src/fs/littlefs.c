#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#define STORAGE_CARRIER_PARTITION_NODE  DT_NODELABEL(storage_carrier_partition)
#define STORAGE_CARRIER_PARTITION_ID    DT_FIXED_PARTITION_ID(STORAGE_CARRIER_PARTITION_NODE)

#define STORAGE_MCU_PARTITION_NODE  DT_NODELABEL(storage_mcu_partition)
#define STORAGE_MCU_PARTITION_ID    DT_FIXED_PARTITION_ID(STORAGE_MCU_PARTITION_NODE)

FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(storage_carrier_mnt_data, 16, 64, 256, 4096, 64);
FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(storage_mcu_mnt_data, 16, 64, 256, 4096, 64);

static struct fs_mount_t storage_carrier_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage_carrier_mnt_data,
    .storage_dev = (void *)STORAGE_CARRIER_PARTITION_ID,
    .mnt_point = "/carrier",
};

static struct fs_mount_t storage_mcu_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage_mcu_mnt_data,
    .storage_dev = (void *)STORAGE_MCU_PARTITION_ID,
    .mnt_point = "/mcu",
};

static int init(struct fs_mount_t *mount, const struct device *device)
{
    if (!device_is_ready(device)) {
        printk("Flash device not ready\n");
        return -ENODEV;
    }
    int rc = fs_mount(mount);
    if (rc == -EIO || rc == -EINVAL || rc == -ENOTSUP) {
        printk("LittleFS not found on partition, formatting %s\n", mount->mnt_point);
        rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)mount->storage_dev, mount->fs_data, 0);
        if (rc < 0) {
            printk("Failed to format LittleFS: %d, %s\n", rc, mount->mnt_point);
            return rc;
        }
        rc = fs_mount(mount);
    }

    if (rc == 0) {
        printk("LittleFS mounted successfully at %s\n", mount->mnt_point);
    } else {
        printk("Failed to mount LittleFS: %d\n", rc);
    }
    return rc;
}

/**
 * @brief Initializes and mounts LittleFS.
 * Handles automatic formatting if the file system is not found.
 */
int littlefs_init(void)
{
    const struct device *flash_carrier_dev = DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(STORAGE_CARRIER_PARTITION_NODE));
    const struct device *flash_mcu_dev = DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(STORAGE_MCU_PARTITION_NODE));
    int rc = init(&storage_carrier_mnt, flash_carrier_dev);
    if ( rc != 0 )
        return rc;
    return init(&storage_mcu_mnt, flash_mcu_dev);
}