#ifndef COLIBRI_RUNTIME_ERROR_HANDLER_H
#define COLIBRI_RUNTIME_ERROR_HANDLER_H

#define defer(end)                  for(int _i_=0;!_i_;_i_+=1,end)
#define defer_alloc(var, size)      void* var=malloc(size);if(var!=NULL)defer(free(var))


int boot_initialize();
void boot_ok();
void boot_error(int error);

#endif //COLIBRI_RUNTIME_ERROR_HANDLER_H
