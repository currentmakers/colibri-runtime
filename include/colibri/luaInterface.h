

#ifndef LUAINTERFACE_H_
#define LUAINTERFACE_H_
#include "lua.h"

int  lua_initialize(void);
int  lua_load_script(lua_State *lua_state, const char *script_path);
bool lua_is_initialized();

#define LUA_INTERFACE_LIBS {"uc", luaopen_uc}
LUAMOD_API int (luaopen_uc) (lua_State *L);

#endif /* LUAINTERFACE_H_ */
