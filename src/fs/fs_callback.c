#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt_callbacks.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#include "colibri-sdk/colibri-events.h"

/* Create every missing parent directory in `path` (path includes filename). */
static void mkdir_parents(const char* path)
{
    char buf[CONFIG_MCUMGR_GRP_FS_PATH_LEN + 1];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Walk each '/' separator, skipping the leading one, and mkdir the prefix. */
    for (char* p = buf + 1; *p; p++)
    {
        if (*p != '/')
        {
            continue;
        }
        *p = '\0';
        int rc = fs_mkdir(buf);
        if (rc != 0 && rc != -EEXIST)
        {
            /* Log but don't abort — fs_open will surface a real error. */
        }
        *p = '/';
    }
}

static enum mgmt_cb_return fs_access_cb(uint32_t event,
                                        enum mgmt_cb_return prev_status,
                                        int32_t* rc, uint16_t* group,
                                        bool* abort_more, void* data, size_t data_size)
{
    if (event == MGMT_EVT_OP_FS_MGMT_FILE_ACCESS &&
        data_size == sizeof(struct fs_mgmt_file_access))
    {
        const struct fs_mgmt_file_access* fa = data;

        if (fa->access == FS_MGMT_FILE_ACCESS_WRITE)
        {
            mkdir_parents(fa->filename);

            if (strncmp(fa->filename, "/mcu/lua_scripts/", 17) == 0 ||
                strncmp(fa->filename, "/carrier/lua_scripts/", 21) == 0)
            {
                printk("Lua script written: %s\n", fa->filename);
                events_publish((event_t){create_user_event(COLIBRI_EVENT_TYPE_LUA_UPDATED, 0)}, 0);
            }
        }
    }

    return MGMT_CB_OK; /* allow the access */
}

static struct mgmt_callback fs_access_callback = {
    .callback = fs_access_cb,
    .event_id = MGMT_EVT_OP_FS_MGMT_FILE_ACCESS,
};

/* Register once at boot, e.g. from main() or a SYS_INIT. */
void fs_upload_mkdir_initialize(void)
{
    mgmt_callback_register(&fs_access_callback);
}
