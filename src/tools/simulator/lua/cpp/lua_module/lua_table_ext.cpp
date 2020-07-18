#include <cstdlib>
#include <assert.h>
#include <cmath>
#include <fstream>
#include <list>
#include <sstream>
#include <unordered_set>

#include "lua_adaptor.h"
#include "lua_table_ext.h"


namespace script {
    namespace lua {
        /**
         * 复制metatable
         * @param L lua state
         * @param index 复制目标
         * @param base_index 复制源
         */
        inline void lua_table_ext_clone_metatable(lua_State *L, int index, int base_index) {
            if (index < 0) {
                index = lua_gettop(L) + 1 + index;
            }

            if (base_index < 0) {
                base_index = lua_gettop(L) + 1 + base_index;
            }

            // 重设metatable
            int res = lua_getmetatable(L, base_index);
            if (0 == res) return;

            if (LUA_EQUAL(L, base_index, -1)) {
                lua_pushvalue(L, index);
                lua_setmetatable(L, index);
                lua_pop(L, 1);
            } else {
                lua_setmetatable(L, index);
            }
        }


        static int lua_table_ext_extend_table(lua_State *L, int ext_begin, int ext_end, int narr, int nrec) {
            std::unordered_set<std::string> rec_keys;
            std::unordered_set<lua_Integer> arr_keys;

            // 先占个位
            lua_pushnil(L);
            int ret_index = lua_gettop(L);

            // 相对位置转绝对位置
            if (ext_begin < 0) ext_begin = ret_index + ext_begin;

            // 相对位置转绝对位置
            if (ext_end < 0) ext_end = ret_index + ext_end;

            int merge_index = ext_end;
            while (merge_index >= ext_begin) {
                /* table is in the stack at index 't' */
                lua_pushnil(L); /* first key */
                while (lua_next(L, merge_index) != 0) {
                    if (lua_isnumber(L, -2)) {
                        arr_keys.insert(lua_tointeger(L, -2));
                    } else if (lua_isstring(L, -2)) {
                        rec_keys.insert(lua_tostring(L, -2));
                    } else {
                        lua_pushvalue(L, -2);
                        rec_keys.insert(lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }

                    lua_pushvalue(L, -2);
                }

                --merge_index;
            }

            int kv_top = lua_gettop(L);

            // 避免rehash
            lua_createtable(L, static_cast<int>(arr_keys.size()) + narr, static_cast<int>(rec_keys.size()) + nrec);
            lua_replace(L, ret_index);

            while (kv_top > ret_index + 1) {
                lua_settable(L, ret_index);
                kv_top -= 2;
            }

            assert(lua_gettop(L) == ret_index);

            if (ext_begin > 0 && ext_end > ext_begin) {
                // 重设metatable
                lua_table_ext_clone_metatable(L, ret_index, ext_begin);
            }

            return 1;
        }

        static int lua_table_ext_extend(lua_State *L) { return lua_table_ext_extend_table(L, 1, lua_gettop(L), 0, 0); }

        static int lua_table_ext_clone_table(lua_State *L, int index, int anarr = 0, int anrec = 0) {
            std::unordered_set<std::string> rec_keys;
            std::unordered_set<lua_Integer> arr_keys;

            // 先占个位
            lua_pushnil(L);
            int ret_index = lua_gettop(L);

            // 如果是相对index, 转为绝对偏移
            if (index < 0) {
                index = ret_index + index;
            }

            {
                /* table is in the stack at index 't' */
                lua_pushnil(L); /* first key */
                while (lua_next(L, index) != 0) {
                    if (lua_isnumber(L, -2)) {
                        arr_keys.insert(lua_tointeger(L, -2));
                    } else if (lua_isstring(L, -2)) {
                        rec_keys.insert(lua_tostring(L, -2));
                    } else {
                        lua_pushvalue(L, -2);
                        rec_keys.insert(lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }

                    if (lua_istable(L, -1)) {
                        lua_table_ext_clone_table(L, -1, 0, 0);
                        lua_remove(L, -2);
                    }

                    lua_pushvalue(L, -2);
                }
            }

            int kv_top = lua_gettop(L);

            // 避免rehash
            lua_createtable(L, static_cast<int>(arr_keys.size()) + anarr, static_cast<int>(rec_keys.size()) + anrec);
            lua_replace(L, ret_index);
            while (kv_top > ret_index + 1) {
                lua_settable(L, ret_index);
                kv_top -= 2;
            }

            assert(lua_gettop(L) == ret_index);

            // 重设metatable
            lua_table_ext_clone_metatable(L, ret_index, index);

            return 1;
        }

        static int lua_table_ext_clone(lua_State *L) {
            int narr = 0, nrec = 0;
            int param_num = lua_gettop(L);

            if (param_num == 0) {
                lua_createtable(L, narr, nrec);
                return 1;
            }


            if (param_num >= 2) {
                narr = static_cast<int>(luaL_checkinteger(L, 2));
            }

            if (param_num >= 3) {
                nrec = static_cast<int>(luaL_checkinteger(L, 3));
            }

            return lua_table_ext_clone_table(L, 1, narr, nrec);
        }

        static int lua_table_ext_create(lua_State *L) {
            int param_num = lua_gettop(L);

            int narr = 0, nrec = 0;

            int data_index = 1;

            if (param_num > 0 && lua_istable(L, 1)) {
                ++data_index;
            }

            if (data_index <= param_num) {
                narr = static_cast<int>(luaL_checkinteger(L, data_index));
                ++data_index;
            }

            if (data_index <= param_num) {
                nrec = static_cast<int>(luaL_checkinteger(L, data_index));
            }

            if (param_num > 0 && lua_istable(L, 1)) {
                return lua_table_ext_extend_table(L, 1, 1, narr, nrec);
            } else {
                lua_createtable(L, narr, nrec);
            }

            return 1;
        }

        static int lua_table_ext_extend_table_recursive(lua_State *L, int ext_begin, int ext_end, int narr, int nrec) {
            std::unordered_set<std::string> rec_keys;
            std::unordered_set<lua_Integer> arr_keys;

            // 先占个位
            lua_pushnil(L);
            int ret_index = lua_gettop(L);

            // 相对位置转绝对位置
            if (ext_begin < 0) ext_begin = ret_index + ext_begin;

            // 相对位置转绝对位置
            if (ext_end < 0) ext_end = ret_index + ext_end;

            int merge_index = ext_end;
            while (merge_index >= ext_begin) {
                /* table is in the stack at index 't' */
                lua_pushnil(L); /* first key */
                while (lua_next(L, merge_index) != 0) {
                    if (lua_isnumber(L, -2)) {
                        arr_keys.insert(lua_tointeger(L, -2));
                    } else if (lua_isstring(L, -2)) {
                        rec_keys.insert(lua_tostring(L, -2));
                    } else {
                        lua_pushvalue(L, -2);
                        rec_keys.insert(lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }

                    lua_pushvalue(L, -2);
                }

                --merge_index;
            }

            // 避免rehash
            lua_createtable(L, static_cast<int>(arr_keys.size()) + narr, static_cast<int>(rec_keys.size()) + nrec);
            lua_replace(L, ret_index);

            int kv_top = lua_gettop(L);

            while (kv_top > ret_index + 1) {
                // 如果value是table,则需要递归拓展
                if (lua_istable(L, -1)) {
                    lua_pushvalue(L, -2);
                    lua_rawget(L, ret_index);
                    if (lua_istable(L, -1)) {
                        lua_insert(L, -2);
                        lua_table_ext_extend_table_recursive(L, -2, -1, 0, 0);
                        lua_replace(L, -3);
                        lua_pop(L, 1);
                    } else {
                        lua_pop(L, 1);
                        lua_table_ext_extend_table_recursive(L, -1, -1, 0, 0);
                        lua_replace(L, -2);
                    }
                }

                lua_settable(L, ret_index);
                kv_top -= 2;
            }

            assert(lua_gettop(L) == ret_index);

            if (ext_begin > 0 && ext_end >= ext_begin) {
                // 重设metatable
                lua_table_ext_clone_metatable(L, ret_index, ext_begin);
            }

            return 1;
        }

        static int lua_table_ext_extend_r(lua_State *L) { return lua_table_ext_extend_table_recursive(L, 1, lua_gettop(L), 0, 0); }

        int lua_table_ext_openlib(lua_State *L) {
            lua_getglobal(L, "table");

            // 创建table : table.create([初始值], [数组元素预留元素个数], [非数组记录预留元素个数])
            lua_pushcfunction(L, lua_table_ext_create);
            lua_setfield(L, -2, "create");

            // 拓展table : table.extend(table1, table2, ...)
            lua_pushcfunction(L, lua_table_ext_extend);
            lua_setfield(L, -2, "extend");

            // 克隆table : table.clone(克隆目标, [附加数组元素预留元素个数], [附加非数组记录预留元素个数])
            lua_pushcfunction(L, lua_table_ext_clone);
            lua_setfield(L, -2, "clone");

            // 递归拓展table : table.extend_r(table1, table2, ...)
            lua_pushcfunction(L, lua_table_ext_extend_r);
            lua_setfield(L, -2, "extend_r");
            return 0;
        }
    }
}
