#include <cstdlib>
#include <assert.h>
#include <chrono>
#include <cstdio>
#include <list>
#include <numeric>

#include <common/string_oprs.h>

#include "lua_adaptor.h"
#include "lua_time_ext.h"


namespace script {
    namespace lua {

        static int lua_time_ext_now_ms(lua_State *L) {
            std::chrono::milliseconds now_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());

            if (static_cast<std::chrono::milliseconds>(std::numeric_limits<lua_Integer>::max()) < now_ms) {
                char n[32] = {0};
                UTIL_STRFUNC_SNPRINTF(n, sizeof(n), "%lld", static_cast<long long>(now_ms.count()));

                lua_getglobal(L, "tonumber");
                lua_pushstring(L, n);
                lua_call(L, 1, 1);
            } else {
                lua_pushinteger(L, static_cast<lua_Integer>(now_ms.count()));
            }

            return 1;
        }

        static int lua_time_ext_now_us(lua_State *L) {
            std::chrono::microseconds now_ms =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch());

            if (static_cast<std::chrono::microseconds>(std::numeric_limits<lua_Integer>::max()) < now_ms) {
                char n[32] = {0};
                UTIL_STRFUNC_SNPRINTF(n, sizeof(n), "%lld", static_cast<long long>(now_ms.count()));

                lua_getglobal(L, "tonumber");
                lua_pushstring(L, n);
                lua_call(L, 1, 1);
            } else {
                lua_pushinteger(L, static_cast<lua_Integer>(now_ms.count()));
            }

            return 1;
        }

        int lua_time_ext_openlib(lua_State *L) {
            int top = lua_gettop(L);

            luaL_Reg lib_funcs[] = {{"now_ms", lua_time_ext_now_ms}, {"now_us", lua_time_ext_now_us}, {NULL, NULL}};

#if LUA_VERSION_NUM <= 501
            luaL_register(L, "time_ext", lib_funcs);
#else
            luaL_newlib(L, lib_funcs);
            lua_setglobal(L, "time_ext");
#endif

            lua_settop(L, top);
            return 0;
        }
    }
}
