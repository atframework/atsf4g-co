#ifndef SCRIPT_LUA_LUATIMEEXT
#define SCRIPT_LUA_LUATIMEEXT

#pragma once

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace script {
    namespace lua {
        int lua_time_ext_openlib(lua_State *L);
    }
}

#endif
