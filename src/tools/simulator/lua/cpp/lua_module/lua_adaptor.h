#ifndef SCRIPT_LUA_LUAADAPTOR
#define SCRIPT_LUA_LUAADAPTOR

#pragma once

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#ifndef LUA_UNSIGNED
typedef unsigned int lua_Unsigned;
#define LUA_UNSIGNED unsigned int

#define lua_pushunsigned(L, n) lua_pushinteger(L, static_cast<lua_Integer>(n))

#elif LUA_VERSION_NUM >= 503
#define lua_pushunsigned(L, n) lua_pushinteger(L, static_cast<lua_Integer>(n))

#endif


#if LUA_VERSION_NUM <= 501
#define LUA_GET_TABLE_LEN(VAR, L, index) VAR = lua_objlen(L, index)
#define LUA_EQUAL(L, index1, index2) lua_equal(L, index1, index2)

#else
#define LUA_GET_TABLE_LEN(VAR, L, index)             \
                                                     \
    lua_len(L, index);                               \
    VAR = static_cast<size_t>(lua_tointeger(L, -1)); \
    lua_pop(L, 1);


#define LUA_EQUAL(L, index1, index2) lua_compare(L, index1, index2, LUA_OPEQ)
#endif

#endif
