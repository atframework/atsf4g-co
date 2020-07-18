#ifndef SCRIPT_LUA_LUATABLEEXT
#define SCRIPT_LUA_LUATABLEEXT

#pragma once

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace script {
    namespace lua {
        int lua_table_ext_openlib(lua_State *L);
    }
}

#endif
