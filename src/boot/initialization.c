#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>

#include "colibri-sdk/colibri-events.h"

#include "colibri/boot.h"
#include "colibri/environment.h"
#include "colibri/fs.h"
#include "colibri/leds.h"
#include "colibri/luaInterface.h"
#include "colibri/management.h"
#include "colibri/modbus.h"
#include "colibri/slots.h"
#include "colibri/tasks.h"
#include "colibri/timing.h"
#include "colibri/usb.h"

/*
 * Bounds on the directory walk below. LIST_PENDING_MAX is how many
 * not-yet-visited subdirectories we can remember at once; LIST_PATH_MAX is the
 * longest path we will build. Together they fix this function's stack frame,
 * which no longer depends on how deeply the tree nests.
 */
#define LIST_PENDING_MAX 8
#define LIST_PATH_MAX    128

/*
 * Prints every file under `path`, in unspecified order.
 *
 * Deliberately iterative. The walk is the deepest call path in the firmware:
 * the carrier flash's SPI chip-select hangs off the TCA6424 behind the TCA9548
 * mux, so each fs_readdir() descends through
 * spi_nor -> spi_stm32 -> gpio -> tca6424a -> i2c -> tca954x before returning.
 * A recursive walk stacked a ~540-byte frame per directory level on top of
 * that, and overflowed the 2 KB main stack at two levels of nesting. Keeping
 * the pending list in a fixed array makes the cost constant instead.
 */
static void list_directory(const char* path)
{
    char pending[LIST_PENDING_MAX][LIST_PATH_MAX];
    int pending_count = 0;
    int skipped = 0;

    if (strlen(path) >= LIST_PATH_MAX)
    {
        printk("Path too long to list: %s\n", path);
        return;
    }
    strcpy(pending[pending_count++], path);

    while (pending_count > 0)
    {
        /*
         * Pop into a local copy: the slot is reused by the pushes below, and
         * order does not matter, so taking the newest entry is fine.
         */
        char dir_path[LIST_PATH_MAX];
        strcpy(dir_path, pending[--pending_count]);

        struct fs_dir_t dir;
        fs_dir_t_init(&dir);
        int rc = fs_opendir(&dir, dir_path);
        if (rc < 0)
        {
            printk("Failed to open directory %s: %d\n", dir_path, rc);
            continue;
        }

        while (1)
        {
            struct fs_dirent entry;
            rc = fs_readdir(&dir, &entry);
            if (rc < 0)
            {
                printk("Failed to read directory %s: %d\n", dir_path, rc);
                break;
            }
            if (entry.name[0] == 0)
            {
                break;
            }

            if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            {
                continue;
            }

            char full_path[LIST_PATH_MAX];
            int len = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry.name);
            if (len < 0 || len >= (int)sizeof(full_path))
            {
                printk("  %s/%s (path too long, skipped)\n", dir_path, entry.name);
                skipped++;
                continue;
            }

            if (entry.type == FS_DIR_ENTRY_DIR)
            {
                printk("  %s/\n", full_path);
                if (pending_count < LIST_PENDING_MAX)
                {
                    strcpy(pending[pending_count++], full_path);
                }
                else
                {
                    /* Say so rather than silently truncating the listing. */
                    printk("  %s/ not descended: more than %d directories pending\n",
                           full_path, LIST_PENDING_MAX);
                    skipped++;
                }
            }
            else
            {
                printk("  %s (size: %zu)\n", full_path, entry.size);
            }
        }
        fs_closedir(&dir);
    }

    if (skipped)
    {
        printk("%s: listing incomplete, %d entr%s skipped\n",
               path, skipped, skipped == 1 ? "y" : "ies");
    }
}

int boot_initialize()
{
    int error;
    slot_count_initialize();

    error = events_initialize();
    if (error)
    {
        printk("Unable to initialize event system. Aborting boot.\n");
        return error;
    }
    error = led_initialize();
    if (error)
    {
        printk("boot: Unable to initialize LEDs. Aborting boot.\n");
        return error;
    }
    error = rgb_initialize();
    if (error)
    {
        printk("boot: Unable to initialize Neopixel LEDs. Aborting boot.\n");
        return error;
    }
    error = slots_initialize();
    if (error)
    {
        printk("boot: Unable to initialize slots: %d. Aborting boot.\n", error);
        return error;
    }
    error = nvs_initialize();
    if (error)
    {
        printk("boot: Unable to initialize NVS: %d. Aborting boot.\n", error);
        return error;
    }
    error = modbus_initialize();
    if (error)
    {
        printk("boot: Unable to initialize Modbus: %d. Aborting boot.\n", error);
        return error;
    }

    printk("Listing directories\n");
    list_directory("/carrier");
    list_directory("/mcu");
    printk("\n");

    // TODO: Maybe figure out if we can enable RTC and have an actual date/time in there as well.
    printk("Writing start note to carrier board\n");
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, "/mcu/startlog.txt", FS_O_RDWR | FS_O_CREATE | FS_O_APPEND);
    if (rc == 0)
    {
        char entry[] = "Start log entry.\n";
        fs_write(&file, entry, strlen(entry));
        fs_close(&file);
    }
    else
    {
        printk("Unable to write to start log.\n");
    }
    k_busy_wait(100);
    error = environment_initialize();
    if (error)
    {
        printk("Unable to initialize environment. Aborting boot.\n");
        return error;
    }
    k_busy_wait(100);

    error = lua_initialize();
    if (error)
    {
        printk("Unable to initialize Lua. Aborting boot.\n");
    }
    k_busy_wait(100);

    error = supervisor_initialize();
    if (error)
    {
        printk("Unable to initialize supervisor. Aborting boot.\n");
        return error;
    }
    // Initialize the RGB updater thread.
    rgb_updater_initialize();

    // Initialize the I/O thread.
    error = io_initialize();
    if (error)
    {
        printk("Unable to initialize IO. Aborting boot.\n");
        return error;
    }

    error = usb_initialize();
    if (error != 0)
    {
        printk("Failed to enable USB stack. Ignoring.\n");
    }

    error = management_initialize();
    if (error != 0)
    {
        printk("Failed to enable management. Ignoring.\n");
    }

    error = timing_initialize();
    if (error)
    {
        printk("Unable to initialize timing. Aborting boot.\n");
    }
    return error;
}
