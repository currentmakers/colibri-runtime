#ifndef COLIBRI_RUNTIME_ENVIRONMENT_H
#define COLIBRI_RUNTIME_ENVIRONMENT_H

#define ENV_FILE_PATH        "/carrier/environment.txt"
#define ENV_MAX_SIZE 512


typedef void (*env_callback_t)(char *, size_t length, void*);

int environment_initialize();
void environment_path_read(char *env_variable, env_callback_t callback, void* user_data );

#endif //COLIBRI_RUNTIME_ENVIRONMENT_H
