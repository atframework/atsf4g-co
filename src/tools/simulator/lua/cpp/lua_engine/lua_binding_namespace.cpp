#include <cstdlib>
#include <sstream>

#include "lua_binding_namespace.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace script {
    namespace lua {
        lua_binding_namespace::lua_binding_namespace() : lua_state_(NULL), base_stack_top_(0), this_ns_(0) {}

        lua_binding_namespace::lua_binding_namespace(const char *namespace_, lua_State *L)
            : lua_state_(NULL), base_stack_top_(0), this_ns_(0) {
            open(namespace_, L);
        }

        lua_binding_namespace::lua_binding_namespace(const char *namespace_, lua_binding_namespace &ns) {
            lua_state_ = ns.lua_state_;
            this_ns_ = ns.this_ns_;
            base_stack_top_ = 0;
            build_ns_set(namespace_);
            find_ns();
            ns_.insert(ns_.begin(), ns.ns_.begin(), ns.ns_.end());
        }

        lua_binding_namespace::~lua_binding_namespace() { close(); }

        lua_binding_namespace::lua_binding_namespace(lua_binding_namespace &ns) : lua_state_(NULL), base_stack_top_(0), this_ns_(0) {
            (*this) = ns;
        }

        lua_binding_namespace &lua_binding_namespace::operator=(lua_binding_namespace &ns) {
            this_ns_ = ns.this_ns_;
            ns_ = ns.ns_;
            base_stack_top_ = ns.base_stack_top_;
            lua_state_ = ns.lua_state_;

            ns.base_stack_top_ = 0;
            return (*this);
        }


        bool lua_binding_namespace::open(const char *namespace_, lua_State *L) {
            close();

            lua_state_ = L;
            if (NULL == namespace_) {
                return true;
            }
            
            ns_.clear();
            build_ns_set(namespace_);

            base_stack_top_ = base_stack_top_ ? base_stack_top_ : lua_gettop(lua_state_);
            return find_ns();
        }

        void lua_binding_namespace::close() {
            if (0 != base_stack_top_) lua_settop(lua_state_, base_stack_top_);

            base_stack_top_ = 0;
            ns_.clear();
        }

        int lua_binding_namespace::get_namespace_table() {
            if (this_ns_) return this_ns_;
#ifdef LUA_RIDX_GLOBALS
            base_stack_top_ = base_stack_top_ ? base_stack_top_ : lua_gettop(lua_state_);
            lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
            return this_ns_ = lua_gettop(lua_state_);
#else
            return this_ns_ = LUA_GLOBALSINDEX;
#endif
        }

        lua_binding_namespace::self_type &lua_binding_namespace::add_const(const char *const_name, const char *n, size_t s) {
            lua_State *state = get_lua_state();
            lua_pushstring(state, const_name);
            lua_pushlstring(state, n, s);
            lua_settable(state, get_namespace_table());

            return *this;
        }

        lua_State *lua_binding_namespace::get_lua_state() { return lua_state_; }

        int lua_binding_namespace::__static_method_wrapper(lua_State *L) {
            static_method fn = reinterpret_cast<static_method>(lua_touserdata(L, lua_upvalueindex(1)));
            if (NULL == fn) {
                WLOGERROR("lua try to call static method but fn not set.\n");
                return 0;
            }

            return fn(L);
        }

        void lua_binding_namespace::build_ns_set(const char *namespace_) {
            // strtok(str, ".") is not thread safe
            const char *s = namespace_, *e = namespace_;
            while (*e) {
                if ('.' == *e) {
                    ns_.push_back(std::string(s, e));
                    s = e + 1;
                }

                ++e;
            }
            if (s < e) ns_.push_back(std::string(s, e));
        }

        bool lua_binding_namespace::find_ns() {
            int cur_ns = get_namespace_table();

            for (std::string &ns : ns_) {
                lua_pushlstring(lua_state_, ns.c_str(), ns.size());
                lua_gettable(lua_state_, cur_ns);
                if (lua_isnil(lua_state_, -1)) {
                    lua_pop(lua_state_, 1);
                    lua_newtable(lua_state_);
                    int top = lua_gettop(lua_state_);
                    lua_pushlstring(lua_state_, ns.c_str(), ns.size());
                    lua_pushvalue(lua_state_, top);
                    lua_settable(lua_state_, cur_ns);
                }

                if (0 == lua_istable(lua_state_, -1)) {
                    return false;
                }
                cur_ns = lua_gettop(lua_state_);
            }

            this_ns_ = cur_ns;
            return true;
        }
    }
}
