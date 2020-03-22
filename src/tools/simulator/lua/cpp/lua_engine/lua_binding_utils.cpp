#include <cmath>
#include <cstdlib>
#include <ctime>
#include <list>
#include <sstream>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "lua_binding_utils.h"

namespace script {
    namespace lua {
        lua_auto_block::lua_auto_block(lua_State *L) : state_(L), stack_top_(lua_gettop(state_)) {}

        lua_auto_block::~lua_auto_block() { lua_settop(state_, stack_top_); }

        void lua_auto_block::null_call() {}

#if !(defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI)
        std::string lua_binding_userdata_generate_metatable_name() {
            static int clazz_idx = 0;
            std::stringstream ss;
            ss << "lua metatable " << (++clazz_idx);
            return ss.str();
        }
#endif

        namespace fn {
            int get_pcall_hmsg(lua_State *L) {
                if (NULL == L) return 0;

                lua_getglobal(L, "stackdump");
                if (!lua_isfunction(L, -1)) {
                    lua_pop(L, 1);
                    lua_pushcfunction(L, lua_stackdump);
                }

                return lua_gettop(L);
            }

            bool load_item(lua_State *L, const std::string &path, bool auto_create_table) {
#ifdef LUA_RIDX_GLOBALS
                lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
                bool res = load_item(L, path, -1, auto_create_table);
                lua_remove(L, -2);
                return res;
#else
                return load_item(L, path, LUA_GLOBALSINDEX, auto_create_table);
#endif
            }

            bool load_item(lua_State *L, const std::string &path, int table_index, bool auto_create_table) {
                int res = true;
                std::list<std::string> idents;
                // strtok(str, ".") is not thread safe
                const char *s = path.c_str(), *e = path.c_str();
                while (*e) {
                    if ('.' == *e) {
                        idents.push_back(std::string(s, e));
                        s = e + 1;
                    }

                    ++e;
                }
                if (s < e) idents.push_back(std::string(s, e));

                lua_pushvalue(L, table_index);
                for (std::list<std::string>::iterator iter = idents.begin(); res && iter != idents.end(); ++iter) {
                    lua_getfield(L, -1, iter->c_str());
                    if (lua_isnil(L, -1)) {
                        if (auto_create_table) {
                            lua_pop(L, 1);
                            lua_newtable(L);
                            lua_pushvalue(L, -1);
                            lua_setfield(L, -3, iter->c_str());
                        } else {
                            res = false;
                        }
                    }

                    lua_remove(L, -2);
                }

                return res && !lua_isnil(L, -1);
            }

            bool remove_item(lua_State *L, const std::string &path) {
#ifdef LUA_RIDX_GLOBALS
                lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
                bool res = remove_item(L, path, -1);
                lua_pop(L, 1);
                return res;
#else
                return remove_item(L, path, LUA_GLOBALSINDEX);
#endif
            }

            bool remove_item(lua_State *L, const std::string &path, int table_index) {
                lua_auto_block block(L);

                std::list<std::string> idents;
                // strtok(str, ".") is not thread safe
                const char *s = path.c_str(), *e = path.c_str();
                while (*e) {
                    if ('.' == *e) {
                        idents.push_back(std::string(s, e));
                        s = e + 1;
                    }

                    ++e;
                }
                if (s < e) idents.push_back(std::string(s, e));

                if (idents.empty()) return true;

                std::string last_ident = idents.back();
                idents.pop_back();

                lua_pushvalue(L, table_index);
                for (std::string &ident : idents) {
                    lua_getfield(L, -1, ident.c_str());
                    if (lua_isnil(L, -1)) {
                        return true;
                    }
                }

                lua_pushnil(L);
                lua_setfield(L, -2, last_ident.c_str());

                return true;
            }

            void print_stack(lua_State *L) {
                int iArgs = lua_gettop(L);
                // LUALOGCALL("STACK TOP:%d", iArgs);

                std::stringstream ss;
                for (int i = 1; i <= iArgs; i++) {
                    ss << i << ":";

                    if (lua_istable(L, i)) {
                        ss << "table";
                    } else if (lua_isnone(L, i)) {
                        ss << "none";
                    } else if (lua_isnil(L, i)) {
                        ss << "nil";
                    } else if (lua_isboolean(L, i)) {
                        if (lua_toboolean(L, i) != 0)
                            ss << "true";
                        else
                            ss << "false";
                    } else if (lua_isfunction(L, i)) {
                        ss << "function";
                    } else if (lua_islightuserdata(L, i)) {
                        ss << "lightuserdata";
                    } else if (lua_isthread(L, i)) {
                        ss << "thread";
                    } else {
                        const char *tinfo = lua_tostring(L, i);
                        if (tinfo)
                            ss << tinfo;
                        else
                            ss << lua_typename(L, lua_type(L, i));
                    }

                    if (i != iArgs) {
                        ss << " |";
                    }
                }

                WLOGINFO("STACK :%s", ss.str().c_str());
            }

            void print_traceback(lua_State *L, const std::string &msg) {
                lua_auto_block autoBlock(L);
                WLOGINFO("TRACEBACK:%s\n%s", msg.c_str(), lua_stackdump_to_string(L).c_str());
            }

            bool exec_file(lua_State *L, const char *file_path) {
                lua_auto_block autoBlock(L);

                int hmsg = get_pcall_hmsg(L);
                if (luaL_loadfile(L, file_path) || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
                    WLOGERROR("%s", luaL_checkstring(L, -1));
                    return false;
                }

                return true;
            }

            bool exec_code(lua_State *L, const char *codes) {
                lua_auto_block autoBlock(L);

                int hmsg = get_pcall_hmsg(L);
                if (luaL_loadstring(L, codes) || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
                    WLOGERROR("%s", luaL_checkstring(L, -1));
                    return false;
                }

                return true;
            }

            int lua_stackdump(lua_State *L) {
                int top = lua_gettop(L);

#if LUA_VERSION_NUM >= 502
                luaL_traceback(L, L, NULL, 1);
#else
                lua_getglobal(L, "debug");

                lua_getfield(L, -1, "traceback");
                lua_remove(L, -2);

                if (top > 0) {
                    lua_pushvalue(L, top);
                } else {
                    lua_pushnil(L);
                }
                lua_pushinteger(L, 1);

                if (lua_pcall(L, 2, LUA_MULTRET, 0) != 0) {
                    WLOGERROR("%s", luaL_checkstring(L, -1));
                }
#endif
                return lua_gettop(L) - top;
            }

            std::string lua_stackdump_to_string(lua_State *L) {
                std::stringstream ss;

                int index = 1;
                lua_Debug ar;
                while (lua_getstack(L, index, &ar)) {
                    lua_getinfo(L, "Snl", &ar);

                    ss << "    ";

                    if (ar.short_src[0]) {
                        ss << static_cast<const char *>(ar.short_src);
                    }

                    if (ar.name) {
                        ss << "(" << static_cast<const char *>(ar.name) << ")";
                    }

                    ss << ":" << ar.currentline << std::endl;

                    ++index;
                }

                return ss.str();
            }
        }  // namespace fn
    }      // namespace lua
}  // namespace script
