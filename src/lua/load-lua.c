#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include "lua.h"
#include "lauxlib.h"

static char script_cache[8192];

int lua_load_script(lua_State *lua_state, const char* script_path)
{
    struct fs_file_t file;
    int rc;
    ssize_t bytes_read;

    fs_file_t_init(&file);

    rc = fs_open(&file, script_path, FS_O_READ);
    if (rc < 0)
    {
        printk("Failed to open script file: %s (error: %d)\n", script_path, rc);
        return rc;
    }

    bytes_read = fs_read(&file, script_cache, sizeof(script_cache) - 1);
    if (bytes_read < 0)
    {
        printk("Failed to read script file: %s (error: %zd)\n", script_path, bytes_read);
        fs_close(&file);
        return (int)bytes_read;
    }

    script_cache[bytes_read] = '\0';

    fs_close(&file);

    if (luaL_dostring(lua_state, script_cache) != LUA_OK)
    {
        printk("Failed to load script: %s\n", lua_tostring(lua_state, -1));
        lua_close(lua_state);
        return -ESRCH;
    }
    return 0;
}