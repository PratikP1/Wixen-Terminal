/* lua_engine.h — Lua 5.5 plugin engine */
#ifndef WIXEN_CONFIG_LUA_ENGINE_H
#define WIXEN_CONFIG_LUA_ENGINE_H

#include <stdbool.h>

typedef struct WixenLuaEngine WixenLuaEngine;

/* Create a sandboxed Lua engine */
WixenLuaEngine *wixen_lua_create(void);
void wixen_lua_destroy(WixenLuaEngine *engine);

/* Load and execute a Lua script file */
bool wixen_lua_exec_file(WixenLuaEngine *engine, const char *path);

/* Execute a Lua string */
bool wixen_lua_exec_string(WixenLuaEngine *engine, const char *code);

/* Call a global function with no args, no return. Returns false if function doesn't exist. */
bool wixen_lua_call(WixenLuaEngine *engine, const char *func_name);

/* Call a global function with one string arg */
bool wixen_lua_call_str(WixenLuaEngine *engine, const char *func_name, const char *arg);

/* Get a global string variable. Returns heap-allocated string or NULL. Caller frees. */
char *wixen_lua_get_string(WixenLuaEngine *engine, const char *name);

/* Get a global integer variable. Returns default_val if not found. */
int wixen_lua_get_int(WixenLuaEngine *engine, const char *name, int default_val);

/* Get a global boolean. Returns default_val if not found. */
bool wixen_lua_get_bool(WixenLuaEngine *engine, const char *name, bool default_val);

#endif /* WIXEN_CONFIG_LUA_ENGINE_H */
