
#ifndef LUAPROJECTCONFIG_H_
#define LUAPROJECTCONFIG_H_

#include <zephyr/kernel.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include "helper/luaHeap.h"


//This function is called by Lua if it cannot handle an occurred error.
void luaAbort(void);

//If the math module shall be included then this define must be defined.
#define LUA_WITH_MATH

//The size of the heap used by Lua.
#define LUA_HEAP_SIZE (32768)

//These defines are used to protect the memory management calls if several Lua instances in different threads are created.
#define LUA_MEM_ENTER_CRITICAL_SECTION()
#define LUA_MEM_LEAVE_CRITICAL_SECTION()

//Function for printing text from lua.
#define lua_writestring(string,length)       printk("lua: %s",string);
#define lua_writeline()                      printk("lua: \n");
#define lua_writestringerror(string,length)  printk("lua: \033[31m%s\033[0m", string);



#endif /* LUAPROJECTCONFIG_H_ */
