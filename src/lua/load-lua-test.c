#include <zephyr/kernel.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static const char* program = "do\n\
    local next=0\n\
    local state=0\n\
\n\
    local function tick()\n\
        set_rgb(-state)\n\
        state = (state + 1) % 5\n\
    end\n\
\n\
    function init()\n\
        set_rgb(0)\n\
    end\n\
\n\
    function event (event_id, value)\n\
        if next < value then\n\
            tick()\n\
            next = value + 300\n\
        end\n\
    end\n\
\n\
    function loaded()\n\
        set_rgb(0)\n\
    end\n\
\n\
    function unloading()\n\
        set_rgb(0)\n\
    end\n\
end";


int lua_load_script(lua_State *lua_state, const char* script_path)
{
    // TODO: Load script from file instead of hardcoded string
    if (luaL_dostring(lua_state, program) != LUA_OK) {
        printk("Failed to load script: %s\n", lua_tostring(lua_state, -1));
        lua_close(lua_state);
        return -ESRCH;
    }
    return 0;
}