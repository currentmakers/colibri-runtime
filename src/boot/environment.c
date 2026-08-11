#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>

#include "colibri/environment.h"

static char environment[128];

static bool file_exists(const char* filename)
{
    struct fs_dirent ent;
    int error = fs_stat(filename, &ent);
    if (error)
    {
        // file does not exist.
        return false;
    }
    return true;
}

/*
 * Read the entire environment file into the provided buffer (NUL-terminated).
 * Returns 0 on success, negative errno on failure.
 */
static int read_env_file(char* buf, size_t buf_size)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, ENV_FILE_PATH, FS_O_READ);
    if (rc != 0)
    {
        return rc;
    }
    ssize_t n = fs_read(&file, buf, buf_size - 1);
    buf[n] = '\0';
    fs_close(&file);
    return n;
}

static int read_or_create(char* env_buf, const size_t buf_size)
{
    if (!file_exists(ENV_FILE_PATH))
    {
        printk("Missing file. Please upload: %s", ENV_FILE_PATH);
        return -ENOENT;
    }
    int bytes_read = read_env_file(env_buf, buf_size);
    if (bytes_read < 0)
    {
        printk("Unable to read %s: %d\n", ENV_FILE_PATH, bytes_read);
    }
    return bytes_read;
}

static void* find(void* buffer, char* str, int bufsize)
{
    for (int i = 0; i < bufsize; i++)
    {
        if (strncmp(&buffer[i], str, strlen(str)) == 0)
        {
            return &buffer[i + strlen(str)];
        }
    }
    return NULL;
}

typedef enum { no, yes, equal_sign, value, found } matching_t;

static void token_callback(char* text, size_t size, env_callback_t callback, void* user_data)
{
    int pos =0;
    for (int i=0; i<size; i++)
    {
        char ch = text[i];
        if ( ch == '\r')
            continue;;
        if ( ch == ',' || ch == '\n' || ch == '\0' )
        {
            callback(&text[pos], i-pos, user_data);
            pos = i+1;
        }
    }
    callback(&text[pos], size-pos, user_data);
}

/*
 * Read the ENV_PATH (/carrier/environment.txt) and look for `env_var`.
 * If `env_var` is found, then tokenize its value, comma-separated, no
 * support for quoted strings.
 */
void environment_path_read(char* env_var, env_callback_t callback, void* user_data)
{
    for (int i = 0; i < sizeof(environment); i++)
    {
        char* substr = &environment[i];
        if (strncmp(env_var, substr, strlen(env_var)) == 0)
        {
            if (substr[strlen(env_var)] == '=')
            {
                char* start = &substr[strlen(env_var)+1];
                // key found, time to extract value
                char* found = strpbrk(start, "\n\r\0");
                if ( found != NULL)
                {
                    size_t length = found - start;
                    token_callback(start, length, callback, user_data);
                    return;
                }
            }
        }
    }
}

int environment_initialize()
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int error = fs_open(&file, ENV_FILE_PATH, FS_O_READ);
    if (error)
    {
        printk("Unable to open environment file: %s (%d)\n", ENV_FILE_PATH, error);
        return error;
    }
    int bytes_read = fs_read(&file, environment, sizeof(environment));
    if (bytes_read == sizeof(environment))
    {
        printk("Environment space is exhausted, and environment.txt can not be fully read.");
        return -ENOMEM;
    }
    fs_close(&file);
    return 0;
}
