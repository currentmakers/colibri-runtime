#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "colibri-sdk/colibri.h"
#include "colibri-sdk/colibri-events.h"
#include "colibri/luaInterface.h"

static lua_State* lua_state;

#define ENV_FILE_PATH        "/carrier/environment.txt"
#define DEFAULT_LUA_PATH     "/mcu/lua_scripts/,/carrier/lua_scripts/"
#define DEFAULT_LUA_INIT     "blinky.lua"
#define DEFAULT_ENV_CONTENT  "LUA_PATH=" DEFAULT_LUA_PATH "\nLUA_INIT=" DEFAULT_LUA_INIT "\n"

#define LUA_PATH_MAX  128
#define ENV_FILE_MAX  512
#define FULL_PATH_MAX 256

static char lua_path[LUA_PATH_MAX] = DEFAULT_LUA_PATH;

void lua_event(event_t event, int64_t value)
{
    lua_getglobal(lua_state, "event");

    if (!lua_isfunction(lua_state, -1))
    {
        printk("Error: 'event' is not a defined Lua function\n");
        lua_pop(lua_state, 1);
        return;
    }

    lua_pushinteger(lua_state, event.value);
    lua_pushinteger(lua_state, value);

    if (lua_pcall(lua_state, 2, 0, 0) != LUA_OK)
    {
        printk("Lua Error: %s\n", lua_tostring(lua_state, -1));
        lua_pop(lua_state, 1);
        return;
    }

    lua_gc(lua_state, LUA_GCCOLLECT, 0);
}

int lua_publish(lua_State* L)
{
    int32_t event = (int32_t)luaL_checknumber(L, 1);
    int64_t value = (int64_t)luaL_checknumber(L, 2);
    event_t ev;
    ev.value = event;
    events_publish(ev, value);
    return 1;
}

int lua_event_get(lua_State* L)
{
    int32_t event = (int32_t)luaL_checknumber(L, 1);
    event_t ev;
    ev.value = event;
    int64_t value = events_get(ev);
    // TODO/NOTE/WARNING: lua_Number is a floating point type, which may not be suitable for representing large integer values. We need to be very aware of this. Update documentation.
    lua_pushnumber(L, (lua_Number)value);
    return 1;
}


int lua_subscribe(lua_State* L)
{
    int32_t event = (int32_t)luaL_checknumber(L, 1);
    uint32_t index = (uint32_t)events_subscribe(event, lua_event);
    lua_pushinteger(L, index);
    return 1;
}

int lua_unsubscribe(lua_State* L)
{
    int32_t index = (int32_t)luaL_checknumber(L, 1);
    events_unsubscribe((void*)index);
    return 1;
}

static const luaL_Reg system_api[] = {
    {"publish", lua_publish},
    {"subscribe", lua_subscribe},
    {"unsubscribe", lua_unsubscribe},
    {"event_value", lua_event_get},
    {NULL, NULL}
};

/*
 * Strip leading/trailing whitespace (in-place). Returns pointer to the
 * first non-whitespace character. The original buffer is mutated to
 * terminate the string at the last non-whitespace character.
 */
static char* trim(char* s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0)
    {
        char c = s[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        {
            break;
        }
        s[--len] = '\0';
    }
    return s;
}

/*
 * Create environment.txt with sensible defaults so subsequent boots find
 * a usable configuration.
 */
static int create_default_environment_file(void)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, ENV_FILE_PATH, FS_O_CREATE | FS_O_WRITE);
    if (rc != 0)
    {
        printk("lua: failed to create %s: %d\n", ENV_FILE_PATH, rc);
        return rc;
    }
    const char* content = DEFAULT_ENV_CONTENT;
    ssize_t written = fs_write(&file, content, strlen(content));
    fs_close(&file);
    if (written < 0)
    {
        printk("lua: failed to write defaults to %s: %zd\n", ENV_FILE_PATH, written);
        return (int)written;
    }
    return 0;
}

/*
 * Read the entire environment file into the provided buffer (NUL-terminated).
 * Returns 0 on success, negative errno on failure.
 */
static int read_environment_file(char* buf, size_t buf_size)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, ENV_FILE_PATH, FS_O_READ);
    if (rc != 0)
    {
        return rc;
    }
    ssize_t n = fs_read(&file, buf, buf_size - 1);
    fs_close(&file);
    if (n < 0)
    {
        return (int)n;
    }
    buf[n] = '\0';
    return 0;
}

/*
 * Parse a single KEY=VALUE line from the environment file and apply it.
 * The 'line' buffer is mutated (trimmed).
 *
 * Returns a newly-allocated copy of the LUA_INIT value (caller must free),
 * or NULL if the line is not LUA_INIT.
 */
static char* parse_env_line(char* line)
{
    line = trim(line);
    if (line[0] == '\0' || line[0] == '#')
    {
        return NULL;
    }
    char* eq = strchr(line, '=');
    if (eq == NULL)
    {
        return NULL;
    }
    *eq = '\0';
    char* key = trim(line);
    char* value = trim(eq + 1);

    if (strcmp(key, "LUA_PATH") == 0)
    {
        strncpy(lua_path, value, sizeof(lua_path) - 1);
        lua_path[sizeof(lua_path) - 1] = '\0';
        return NULL;
    }
    if (strcmp(key, "LUA_INIT") == 0)
    {
        return strdup(value);
    }
    return NULL;
}

/*
 * Resolve 'script' to a usable path:
 *   - If absolute (starts with '/'), use as-is. Write the value into 'out' argument.
 *   - Otherwise, walk the comma-separated values in lua_path, and check if the file 
 *     is in any of those locations. If file is found, then write the full path  
 *     into 'out' (max size 'out_size'). 
 *     
 * Returns 0 on success. Return negative number on any system level error.
 */
static int resolve_script_path(const char* script, char* out, size_t out_size)
{
    if (script[0] == '/')
    {
        if (strlen(script) >= out_size)
        {
            return -ENAMETOOLONG;
        }
        strcpy(out, script);
        return 0;
    }

    /* Relative path: search through lua_path directories */
    const char* path_copy = strdup(lua_path);
    if (path_copy == NULL)
    {
        return -ENOMEM;
    }

    char* saveptr = NULL;
    for (char* dir = strtok_r(path_copy, ",", &saveptr);
         dir != NULL;
         dir = strtok_r(NULL, ",", &saveptr))
    {
        dir = trim(dir);
        if (dir[0] == '\0')
        {
            continue;
        }

        /* Construct full path: dir + script */
        size_t dir_len = strlen(dir);
        size_t script_len = strlen(script);
        size_t total_len = dir_len + script_len;

        /* Add 1 for potential '/' separator */
        if (dir[dir_len - 1] != '/')
        {
            total_len++;
        }

        if (total_len >= out_size)
        {
            free(path_copy);
            return -ENAMETOOLONG;
        }

        strcpy(out, dir);
        if (dir[dir_len - 1] != '/')
        {
            strcat(out, "/");
        }
        strcat(out, script);

        /* Check if file exists */
        struct fs_dirent entry;
        int rc = fs_stat(out, &entry);
        if (rc == 0 && entry.type == FS_DIR_ENTRY_FILE)
        {
            free(path_copy);
            return 0;
        }
    }

    free(path_copy);
    return -ENOENT;
}

int lua_initialize()
{
    lua_state = luaL_newstate();
    if (lua_state == NULL)
    {
        printk("lua: cannot create state: not enough memory\n");
        return -ENOMEM;
    }

    luaL_openlibs(lua_state);
    lua_register_event_constants(lua_state);
    lua_install_uc_globals(lua_state);

    // Initialize user scripts:
    // 1. Look for environment.txt on the Carrier Flash.
    // 2. If missing, create one with the defaults
    //    (LUA_PATH=lua_scripts/, LUA_INIT=blinky.lua).
    // 3. Read it.
    // 4. Apply LUA_PATH= to the static lua_path[] used to resolve relative
    //    script names.
    // 5. Load every script listed in LUA_INIT= (comma-separated). Absolute
    //    names are used as-is; relative names are resolved against lua_path.

    char env_buf[ENV_FILE_MAX];
    int error = read_environment_file(env_buf, sizeof(env_buf));
    if (error == -ENOENT)
    {
        printk("lua: %s not found, creating defaults\n", ENV_FILE_PATH);
        error = create_default_environment_file();
        if (error != 0)
        {
            return error;
        }
        error = read_environment_file(env_buf, sizeof(env_buf));
    }
    if (error != 0)
    {
        printk("lua: unable to read %s: %d\n", ENV_FILE_PATH, error);
        return error;
    }

    /*
     * Parse line-by-line. LUA_PATH is captured into lua_path[] immediately;
     * LUA_INIT is stashed so it is acted on only after the whole file has
     * been parsed (so LUA_PATH is in effect regardless of line order). If
     * LUA_INIT appears more than once, the last occurrence wins.
     */
    char* lua_init_value = NULL;
    char* saveptr = NULL;
    for (char* line = strtok_r(env_buf, "\n", &saveptr);
         line != NULL;
         line = strtok_r(NULL, "\n", &saveptr))
    {
        char* init = parse_env_line(line);
        if (init != NULL)
        {
            free(lua_init_value);
            lua_init_value = init;
        }
    }

    if (lua_init_value == NULL) {
        printk("lua: no LUA_INIT entries found in %s\n", ENV_FILE_PATH);
        /* No scripts to load - state is initialized, just nothing
         * subscribed to events yet. */
        return 0;
    }

    /*
     * Walk every comma-separated entry in LUA_INIT and load it. Absolute
     * names are used as-is; relative names are resolved against lua_path.
     * Stop at the first failure so the caller sees a useful error code.
     */
    char *saveptr2 = NULL;
    for (char *tok = strtok_r(lua_init_value, ",", &saveptr2);
         tok != NULL;
         tok = strtok_r(NULL, ",", &saveptr2)) {
        char *script = trim(tok);
        if (script[0] == '\0') {
            continue;
        }
        char full_path[FULL_PATH_MAX];
        error = resolve_script_path(script, full_path, sizeof(full_path));
        if (error != 0) {
            printk("lua: cannot resolve script path '%s': %d\n", script, error);
            break;
        }
        printk("lua: loading script %s\n", full_path);
        error = lua_load_script(lua_state, full_path);
        if (error != 0) {
            printk("lua: failed to load %s: %d\n", full_path, error);
            break;
        }
    }
    free(lua_init_value);
    if (error != 0) {
        /* Script not loaded. */
        printk("lua: unable to load init scripts: %d\n", error);
        return error;
    }
    return 0;
}


// This function is called by Lua if it cannot handle an error that occurred.
void luaAbort()
{
    lua_writestringerror("luaAbort", sizeof("luaAbort"));
    while (1)
    {
    }
}

void lua_install_uc_globals(lua_State* L)
{
    for (const luaL_Reg* r = system_api; r->name != NULL; r++)
    {
        lua_pushcfunction(L, r->func);
        lua_setglobal(L, r->name);
    }
}

LUAMOD_API int luaopen_uc(lua_State* L)
{
    luaL_newlib(L, system_api);
    return 1;
}

void lua_register_event_constants(lua_State* L)
{
    lua_newtable(L);

    lua_pushstring(L, "UNKNOWN");
    lua_pushinteger(L, 0x0000);
    lua_settable(L, -3);

    lua_pushstring(L, "TIME_PERIOD");
    lua_pushinteger(L, 0x0100);
    lua_settable(L, -3);

    lua_pushstring(L, "TIME");
    lua_pushinteger(L, 0x0200);
    lua_settable(L, -3);

    lua_pushstring(L, "COUNTER");
    lua_pushinteger(L, 0x0300);
    lua_settable(L, -3);

    lua_pushstring(L, "ERROR_CODE");
    lua_pushinteger(L, 0x0400);
    lua_settable(L, -3);

    lua_pushstring(L, "MEASURED_VALUE");
    lua_pushinteger(L, 0x0500);
    lua_settable(L, -3);

    lua_pushstring(L, "COMPUTED_VALUE");
    lua_pushinteger(L, 0x0600);
    lua_settable(L, -3);

    lua_pushstring(L, "SETPOINT");
    lua_pushinteger(L, 0x0700);
    lua_settable(L, -3);

    lua_pushstring(L, "MIN_VALUE");
    lua_pushinteger(L, 0x0800);
    lua_settable(L, -3);

    lua_pushstring(L, "MAX_VALUE");
    lua_pushinteger(L, 0x0900);
    lua_settable(L, -3);

    lua_pushstring(L, "LOW_THRESHOLD");
    lua_pushinteger(L, 0x0A00);
    lua_settable(L, -3);

    lua_pushstring(L, "HIGH_THRESHOLD");
    lua_pushinteger(L, 0x0B00);
    lua_settable(L, -3);

    lua_pushstring(L, "RUN_INDICATION");
    lua_pushinteger(L, 0x0C00);
    lua_settable(L, -3);

    lua_pushstring(L, "ALARM_INDICATION");
    lua_pushinteger(L, 0x0D00);
    lua_settable(L, -3);

    lua_pushstring(L, "RGB_SET");
    lua_pushinteger(L, 0x0E00);
    lua_settable(L, -3);

    lua_setglobal(L, "events");
}
