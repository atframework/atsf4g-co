#include <cstdlib>
#include <cmath>
#include <ctime>
#include <list>
#include <sstream>

#include "lua_engine.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "lua_binding_mgr.h"

#include "../lua_module/lua_table_ext.h"
#include "../lua_module/lua_time_ext.h"

namespace script {
    namespace lua {
        extern int lua_profile_openlib(lua_State *L);

        lua_auto_stats::lua_auto_stats(lua_engine& engine): engine_(&engine) { begin_clock_ = std::chrono::system_clock::now(); }

        lua_auto_stats::~lua_auto_stats() {
            if (NULL != engine_) {
                auto duration = (std::chrono::system_clock::now() - begin_clock_);
                engine_->add_lua_stat_time(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0f);
                // engine_->add_lua_stat_time(abs(end_clock_ - begin_clock_) * 1.0f / CLOCKS_PER_SEC);
            }
        }

        lua_engine::lua_engine(constructor_helper& helper) : state_(helper.L) {
            lua_update_stats_.lua_time = 0.0f;
            lua_update_stats_.run_time = 0.0f;
        }


        lua_engine::~lua_engine() {
            lua_binding_mgr::me()->remove_lua_engine(this);

            if (state_) {
                lua_close(state_);
                state_ = NULL;
            }
        }

        int lua_engine::add_on_inited(std::function<void(lua_State *)> fn) {
            on_inited_.push_back(fn);
            return 0;
        }

        lua_engine::ptr_t lua_engine::create() {
            lua_State* L = luaL_newstate();
            if (NULL == L) {
                return ptr_t();
            }

            constructor_helper helper;
            helper.L = L;
            lua_engine::ptr_t ret = std::make_shared<lua_engine>(helper);
            if (!ret) {
                lua_close(L);
            }

            return ret;
        }

        int lua_engine::init() {
            if (NULL == state_) {
                return -1;
            }

            luaL_openlibs(state_);

            // add 3rdparty librarys
            // add_ext_lib(luaopen_profiler);
            // add_ext_lib(luaopen_bit);
            // add_ext_lib(luaopen_pack);
            // add_ext_lib(luaopen_mime_core);
            // add_ext_lib(luaopen_socket_core);
            // add_ext_lib(luaopen_cjson);
            // add_ext_lib(luaopen_protobuf_c);

            // add inner librarys
            // add_ext_lib(lua_profile_openlib);
            add_ext_lib(lua_table_ext_openlib);
            add_ext_lib(lua_time_ext_openlib);

            // 注册到对象池管理器
            lua_binding_mgr::me()->add_lua_engine(this);

            for (std::function<void(lua_State *)> &fn : on_inited_) {
                fn(state_);
            }
            on_inited_.clear();
            return 0;
        }

        int lua_engine::proc() {
            return lua_binding_mgr::me()->proc(this);
        }

        void lua_engine::add_ext_lib(lua_CFunction regfunc) { add_ext_lib(state_, regfunc); }

        void lua_engine::add_ext_lib(lua_State *L, lua_CFunction regfunc) { regfunc(L); }

        void lua_engine::add_search_path(const std::string &path, bool is_front) { add_search_path(state_, path, is_front); }

        void lua_engine::add_search_path(lua_State *L, const std::string &path, bool is_front) {
            lua_getglobal(L, "package"); /* L: package */
            lua_getfield(L, -1, "path"); /* get package.path, L: package path */
            const char *cur_path = lua_tostring(L, -1);
            if (is_front)
                lua_pushfstring(L, "%s/?.lua;%s/?.luac;%s", path.c_str(), path.c_str(), cur_path); /* L: package path newpath */
            else
                lua_pushfstring(L, "%s;%s/?.lua;%s/?.luac", cur_path, path.c_str(), path.c_str()); /* L: package path newpath */
            lua_setfield(L, -3, "path"); /* package.path = newpath, L: package path */
            lua_pop(L, 2);               /* L: - */
        }

        void lua_engine::add_csearch_path(const std::string &path, bool is_front) { add_csearch_path(state_, path, is_front); }

        void lua_engine::add_csearch_path(lua_State *L, const std::string &path, bool is_front) {
            lua_getglobal(L, "package");  /* L: package */
            lua_getfield(L, -1, "cpath"); /* get package.path, L: package cpath */
            const char *cur_path = lua_tostring(L, -1);
#ifdef WIN32
            if (is_front)
                lua_pushfstring(L, "%s/?.dll;%s", path.c_str(), cur_path); /* L: package path newpath */
            else
                lua_pushfstring(L, "%s;%s/?.dll", cur_path, path.c_str()); /* L: package path newpath */
#else
            if (is_front)
                lua_pushfstring(L, "%s/?.so;%s", path.c_str(), cur_path); /* L: package path newpath */
            else
                lua_pushfstring(L, "%s;%s/?.so", cur_path, path.c_str()); /* L: package path newpath */
#endif
            lua_setfield(L, -3, "cpath"); /* package.cpath = newpath, L: package cpath */
            lua_pop(L, 2);
        }

        void lua_engine::add_lua_loader(lua_CFunction func) { add_lua_loader(state_, func); }

        void lua_engine::add_lua_loader(lua_State *L, lua_CFunction func) {
            if (!func) return;

            // stack content after the invoking of the function
            // get loader table
            lua_getglobal(L, "package"); /* L: package */
            const char *loader_name = "loaders";
            lua_getfield(L, -1, loader_name); /* L: package, loaders */
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                loader_name = "searchers";
                lua_getfield(L, -1, loader_name); /* L: package, searchers */
            }

            // error
            if (lua_isnil(L, -1)) {
                lua_pop(L, 2);
                luaL_error(L, "package.loaders or package.searchers not found");
                return;
            }

            // insert loader into index 2
            lua_pushcfunction(L, func); /* L: package, loaders, func */
            LUA_GET_TABLE_LEN(int len, L, -2);
            for (int i = len + 1; i > 2; --i) {
                lua_rawgeti(L, -2, i - 1); /* L: package, loaders, func, function */
                // we call lua_rawgeti, so the loader table now is at -3
                lua_rawseti(L, -3, i); /* L: package, loaders, func */
            }
            lua_rawseti(L, -2, 2); /* L: package, loaders */

            // set loaders into package
            lua_setfield(L, -2, loader_name); /* L: package */

            lua_pop(L, 1);
        }

        bool lua_engine::run_code(const char *codes) { 
            lua::lua_auto_stats autoLuaStat(*this);

            return run_code(state_, codes); 
        }

        bool lua_engine::run_code(lua_State *L, const char *codes) {
            return fn::exec_code(L, codes);
        }

        bool lua_engine::run_file(const char *file_path) { 
            lua::lua_auto_stats autoLuaStat(*this);

            return run_file(state_, file_path); 
        }

        bool lua_engine::run_file(lua_State *L, const char *file_path) {
            return fn::exec_file(L, file_path);
        }

        lua_State *lua_engine::get_lua_state() { return state_; }

        bool lua_engine::load_item(const std::string &path, bool auto_create_table) {
            lua::lua_auto_stats autoLuaStat(*this);
            return load_item(get_lua_state(), path, auto_create_table);
        }

        bool lua_engine::load_item(const std::string &path, int table_index, bool auto_create_table) {
            lua::lua_auto_stats autoLuaStat(*this);
            return load_item(get_lua_state(), path, table_index, auto_create_table);
        }


        bool lua_engine::load_item(lua_State *L, const std::string &path, bool auto_create_table) {
            return fn::load_item(L, path, auto_create_table);
        }

        bool lua_engine::load_item(lua_State *L, const std::string &path, int table_index, bool auto_create_table) {
            return fn::load_item(L, path, table_index, auto_create_table);
        }

        bool lua_engine::remove_item(const std::string &path) { 
            lua::lua_auto_stats autoLuaStat(*this);
            return remove_item(get_lua_state(), path); 
        }

        bool lua_engine::remove_item(const std::string &path, int table_index) { 
            lua::lua_auto_stats autoLuaStat(*this);
            return remove_item(get_lua_state(), path, table_index); 
        }


        bool lua_engine::remove_item(lua_State *L, const std::string &path) {
            return fn::remove_item(L, path);
        }

        bool lua_engine::remove_item(lua_State *L, const std::string &path, int table_index) {
            return fn::remove_item(L, path, table_index);
        }

        bool lua_engine::load_global_event_trigger(lua_State *L, const std::string &bind_name) {
            lua_getglobal(L, "utils");
            do {
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "event");
                    lua_remove(L, -2);
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, -1, "global");
                        lua_remove(L, -2);
                        if (lua_istable(L, -1)) {
                            lua_getfield(L, -1, "trigger");
                            lua_pushvalue(L, -2);
                            lua_remove(L, -3);
                            lua_pushlstring(L, bind_name.c_str(), bind_name.size());
                            return lua_isfunction(L, -3);
                        }
                    }
                }
            } while (false);

            // 保证增加两个栈对象
            lua_pushnil(L);
            lua_pushnil(L);
            return false;
        }

        bool lua_engine::load_global_event_trigger(const std::string &bind_name) {
            lua::lua_auto_stats autoLuaStat(*this);

            return load_global_event_trigger(get_lua_state(), bind_name);
        }

        int lua_engine::get_pcall_hmsg(lua_State *L) { return fn::get_pcall_hmsg(L); }

        int lua_engine::get_pcall_hmsg() { return get_pcall_hmsg(get_lua_state()); }

        void lua_engine::update_global_timer(float delta) {
            lua_State *state = get_lua_state();
            lua::lua_auto_block block(state);
            lua_auto_stats autoLuaStat(*this);

            int hmsg = get_pcall_hmsg(state);

            lua_getglobal(state, "utils");
            if (lua_istable(state, -1)) {
                lua_getfield(state, -1, "event");
                if (lua_istable(state, -1)) {
                    lua_getfield(state, -1, "update");
                    if (lua_isfunction(state, -1)) {
                        lua_pushnumber(state, delta);

                        if (0 != lua_pcall(state, 1, LUA_MULTRET, hmsg)) {
                            WLOGERROR("[Lua]: %s", luaL_checkstring(state, -1));
                        }
                    }
                }
            }

            lua_update_stats_.run_time += delta;
        }

        void lua_engine::add_lua_stat_time(float delta) { lua_update_stats_.lua_time += delta; }

        std::pair<float, float> lua_engine::get_and_reset_lua_stats() {
            std::pair<float, float> ret = std::make_pair(lua_update_stats_.lua_time, lua_update_stats_.run_time);
            lua_update_stats_.lua_time = lua_update_stats_.run_time = 0.0f;
            return ret;
        }
    }
}
