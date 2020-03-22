#ifndef SCRIPT_LUA_LUABINDINGUTILS
#define SCRIPT_LUA_LUABINDINGUTILS

#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <typeinfo>

#include <config/atframe_utils_build_feature.h>

#include <std/explicit_declare.h>
#include <config/compiler_features.h>

#include "../lua_module/lua_adaptor.h"

#ifndef WLOGERROR
#ifdef _MSC_VER
#define WLOGERROR(fmt, ...) fprintf(stderr, "[ERROR]:" fmt "\r\n", __VA_ARGS__)
#else
#define WLOGERROR(fmt, args...) fprintf(stderr, "[ERROR]:" fmt "\r\n", ##args)
#endif
#endif

#ifndef WLOGINFO
#ifdef _MSC_VER
#define WLOGINFO(fmt, ...) fprintf(stdout, "[INFO]:" fmt "\r\n", __VA_ARGS__)
#else
#define WLOGINFO(fmt, args...) fprintf(stdout, "[INFO]:" fmt "\r\n", ##args)
#endif
#endif

namespace script {
    namespace lua {

        class lua_auto_block {
        public:
            lua_auto_block(lua_State *);

            ~lua_auto_block();

            void null_call();

        private:
            lua_auto_block(const lua_auto_block &src) UTIL_CONFIG_DELETED_FUNCTION;
            const lua_auto_block &operator=(const lua_auto_block &src) UTIL_CONFIG_DELETED_FUNCTION;

            lua_State *state_;
            int stack_top_;
        };

#if !(defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI)
        std::string lua_binding_userdata_generate_metatable_name();
#endif

        template <typename TC>
        struct lua_binding_userdata_info {
            typedef TC value_type;
            typedef std::shared_ptr<value_type> pointer_type;
            typedef std::weak_ptr<value_type> userdata_type;
            typedef userdata_type *userdata_ptr_type;

            static const char *get_lua_metatable_name() { 
#if defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI
                return typeid(value_type).name();
#else
                if (!metatable_name_.empty()) {
                    return metatable_name_.c_str();
                }

                metatable_name_ = lua_binding_userdata_generate_metatable_name();
                return metatable_name_.c_str();
#endif
            }

#if !(defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI)
            static std::string metatable_name_;
#endif
        };

#if !(defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI)
        template <typename TC>
        std::string lua_binding_userdata_info<TC>::metatable_name_;
#endif

        /**
         * 特殊实例（整个对象直接存在userdata里）
         *
         * @tparam  TC  Type of the tc.
         */

        template <typename TC>
        struct lua_binding_placement_new_and_delete {
            typedef TC value_type;

            static void push_metatable(lua_State *L) {
                const char *class_name = lua_binding_userdata_info<value_type>::get_lua_metatable_name();
                luaL_getmetatable(L, class_name);
                if (lua_istable(L, -1)) return;

                lua_pop(L, 1);
                luaL_newmetatable(L, class_name);
                lua_pushcfunction(L, __lua_gc);
                lua_setfield(L, -2, "__gc");
            }

            /**
             * 创建新实例，并把相关的lua对象入栈
             *
             * @param [in,out]  L   If non-null, the lua_State * to process.
             *
             * @return  null if it fails, else a value_type*.
             */

            template <typename... TParams>
            static value_type *create(lua_State *L, TParams &&... params) {
                value_type *obj = new (lua_newuserdata(L, sizeof(value_type))) value_type(std::forward<TParams>(params)...);

                push_metatable(L);
                lua_setmetatable(L, -2);
                return obj;
            }

            /**
             * 垃圾回收方法
             *
             * @author
             * @date    2014/10/25
             *
             * @param [in,out]  L   If non-null, the lua_State * to process.
             *
             * @return  An int.
             */

            static int __lua_gc(lua_State *L) {
                if (0 == lua_gettop(L)) {
                    WLOGERROR("userdata __gc is called without self");
                    return 0;
                }

                // metatable表触发
                if (0 == lua_isuserdata(L, 1)) {
                    lua_remove(L, 1);
                    return 0;
                }

                auto obj =
                    static_cast<value_type *>(luaL_checkudata(L, 1, lua_binding_userdata_info<value_type>::get_lua_metatable_name()));
                if (NULL == obj) {
                    WLOGERROR("try to convert userdata to %s but failed", lua_binding_userdata_info<value_type>::get_lua_metatable_name());
                    return 0;
                }
                // 执行析构，由lua负责释放内存
                obj->~value_type();
                return 0;
            }
        };


        namespace fn {
            int get_pcall_hmsg(lua_State *L);

            bool load_item(lua_State *L, const std::string &path, bool auto_create_table = false);

            bool load_item(lua_State *L, const std::string &path, int table_index, bool auto_create_table = false);

            bool remove_item(lua_State *L, const std::string &path);

            bool remove_item(lua_State *L, const std::string &path, int table_index);

            void print_stack(lua_State *L);

            void print_traceback(lua_State *L, const std::string &msg);

            bool exec_file(lua_State *L, const char *file_path);

            bool exec_code(lua_State *L, const char *codes);

            int lua_stackdump(lua_State *L);

            std::string lua_stackdump_to_string(lua_State *L);
        }
    }
}
#endif
