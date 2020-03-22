#ifndef SCRIPT_LUA_LUAENGINE
#define SCRIPT_LUA_LUAENGINE

#pragma once

#include <assert.h>
#include <chrono>
#include <ctime>
#include <functional>
#include <list>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <design_pattern/noncopyable.h>
#include <std/smart_ptr.h>

#include "lua_binding_utils.h"

namespace script {
    namespace lua {

        class lua_engine;

        /** A lua automatic statistics. */
        struct lua_auto_stats {
            lua_auto_stats(lua_engine& engine);
            ~lua_auto_stats();

            std::chrono::system_clock::time_point begin_clock_;
            lua_engine* engine_;
            // std::chrono::system_clock::time_point end_clock_;
        };

        class lua_engine : public ::util::design_pattern::noncopyable {
        public:
            typedef std::shared_ptr<lua_engine> ptr_t;

        private:
            struct constructor_helper {
                lua_State *L;
            };

        public:
            lua_engine(constructor_helper& helper);
            ~lua_engine();

            int add_on_inited(std::function<void(lua_State *)> fn);

            static ptr_t create();

            int init();

            int proc();

            void add_ext_lib(lua_CFunction regfunc);

            static void add_ext_lib(lua_State *L, lua_CFunction regfunc);

            void add_search_path(const std::string &path, bool is_front = false);

            static void add_search_path(lua_State *L, const std::string &path, bool is_front = false);

            void add_csearch_path(const std::string &path, bool is_front = false);

            static void add_csearch_path(lua_State *L, const std::string &path, bool is_front = false);

            void add_lua_loader(lua_CFunction func);

            static void add_lua_loader(lua_State *L, lua_CFunction func);

            bool run_code(const char *codes);

            static bool run_code(lua_State *L, const char *codes);

            bool run_file(const char *file_path);

            static bool run_file(lua_State *L, const char *file_path);

            lua_State *get_lua_state();

            static bool load_item(lua_State *L, const std::string &path, bool auto_create_table = false);

            static bool load_item(lua_State *L, const std::string &path, int table_index, bool auto_create_table = false);

            bool load_item(const std::string &path, bool auto_create_table = false);

            bool load_item(const std::string &path, int table_index, bool auto_create_table = false);

            static bool remove_item(lua_State *L, const std::string &path);

            static bool remove_item(lua_State *L, const std::string &path, int table_index);

            bool remove_item(const std::string &path);

            bool remove_item(const std::string &path, int table_index);

            /**
             * Loads event trigger.
             * lua栈将会添加三个元素，分别是utils.event.trigger, utils.event.global 和 bind_name
             * @note 注意执行pcall时参数数量要加2
             *
             * @param [in,out]  L   If non-null, the lua_State* to process.
             * @param   bind_name   Name of the bind.
             *
             * @return  true if it succeeds, false if it fails.
             */

            static bool load_global_event_trigger(lua_State *L, const std::string &bind_name);

            /**
             * Loads event trigger.
             * lua栈将会添加三个元素，分别是utils.event.global.trigger, utils.event.global 和 bind_name
             * @note 注意执行pcall时参数数量要加2
             *
             * @param [in,out]  L   If non-null, the lua_State* to process.
             * @param   bind_name   Name of the bind.
             *
             * @return  true if it succeeds, false if it fails.
             */
            bool load_global_event_trigger(const std::string &bind_name);

            /**
             * 用于lua_pcall函数内的hmsg参数值
             *
             * @note 会添加 lua function到lua虚拟机顶层，失败则添加nil，返回0
             * @param [in,out]  L   If non-null, the lua_State* to process.
             *
             * @return  The p call h message.
             */

            static int get_pcall_hmsg(lua_State *L);

            int get_pcall_hmsg();

            void update_global_timer(float delta);
            void add_lua_stat_time(float delta);

            std::pair<float, float> get_and_reset_lua_stats();

        private:
            struct lua_stats {
                float lua_time;
                float run_time;
            };


            lua_State *state_;
            std::list<std::function<void(lua_State *)> > on_inited_;

            lua_stats lua_update_stats_;
        };
    }
}

#endif
