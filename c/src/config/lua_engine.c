/* lua_engine.c — Lua 5.5 plugin engine with sandboxing */
#include "wixen/config/lua_engine.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdlib.h>
#include <string.h>

struct WixenLuaEngine {
    lua_State *L;
};

/* Sandboxed environment: only safe functions */
static void sandbox_lua(lua_State *L) {
    /* Remove dangerous functions */
    lua_pushnil(L); lua_setglobal(L, "os");
    lua_pushnil(L); lua_setglobal(L, "io");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "dofile");
    /* Keep: string, table, math, print, type, pairs, ipairs, tostring, tonumber */
}

WixenLuaEngine *wixen_lua_create(void) {
    WixenLuaEngine *e = (WixenLuaEngine *)calloc(1, sizeof(WixenLuaEngine));
    if (!e) return NULL;

    e->L = luaL_newstate();
    if (!e->L) { free(e); return NULL; }

    luaL_openlibs(e->L);
    sandbox_lua(e->L);

    return e;
}

void wixen_lua_destroy(WixenLuaEngine *engine) {
    if (!engine) return;
    if (engine->L) lua_close(engine->L);
    free(engine);
}

bool wixen_lua_exec_file(WixenLuaEngine *engine, const char *path) {
    if (!engine || !path) return false;
    int status = luaL_dofile(engine->L, path);
    return status == LUA_OK;
}

bool wixen_lua_exec_string(WixenLuaEngine *engine, const char *code) {
    if (!engine || !code) return false;
    int status = luaL_dostring(engine->L, code);
    return status == LUA_OK;
}

bool wixen_lua_call(WixenLuaEngine *engine, const char *func_name) {
    if (!engine || !func_name) return false;
    lua_getglobal(engine->L, func_name);
    if (!lua_isfunction(engine->L, -1)) {
        lua_pop(engine->L, 1);
        return false;
    }
    int status = lua_pcall(engine->L, 0, 0, 0);
    return status == LUA_OK;
}

bool wixen_lua_call_str(WixenLuaEngine *engine, const char *func_name, const char *arg) {
    if (!engine || !func_name) return false;
    lua_getglobal(engine->L, func_name);
    if (!lua_isfunction(engine->L, -1)) {
        lua_pop(engine->L, 1);
        return false;
    }
    lua_pushstring(engine->L, arg ? arg : "");
    int status = lua_pcall(engine->L, 1, 0, 0);
    return status == LUA_OK;
}

char *wixen_lua_get_string(WixenLuaEngine *engine, const char *name) {
    if (!engine || !name) return NULL;
    lua_getglobal(engine->L, name);
    if (!lua_isstring(engine->L, -1)) {
        lua_pop(engine->L, 1);
        return NULL;
    }
    const char *s = lua_tostring(engine->L, -1);
    char *result = NULL;
    if (s) {
        size_t len = strlen(s);
        result = (char *)malloc(len + 1);
        if (result) memcpy(result, s, len + 1);
    }
    lua_pop(engine->L, 1);
    return result;
}

int wixen_lua_get_int(WixenLuaEngine *engine, const char *name, int default_val) {
    if (!engine || !name) return default_val;
    lua_getglobal(engine->L, name);
    if (!lua_isinteger(engine->L, -1)) {
        lua_pop(engine->L, 1);
        return default_val;
    }
    int val = (int)lua_tointeger(engine->L, -1);
    lua_pop(engine->L, 1);
    return val;
}

bool wixen_lua_get_bool(WixenLuaEngine *engine, const char *name, bool default_val) {
    if (!engine || !name) return default_val;
    lua_getglobal(engine->L, name);
    if (!lua_isboolean(engine->L, -1)) {
        lua_pop(engine->L, 1);
        return default_val;
    }
    bool val = lua_toboolean(engine->L, -1) != 0;
    lua_pop(engine->L, 1);
    return val;
}
