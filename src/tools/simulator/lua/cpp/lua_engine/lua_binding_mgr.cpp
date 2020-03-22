#include <cstdlib>

#include "lua_engine.h"

#include "lua_binding_mgr.h"

namespace script {
    namespace lua {
        lua_binding_class_mgr_base::lua_binding_class_mgr_base() { lua_binding_mgr::me()->lua_states_.push_back(this); }

        lua_binding_class_mgr_base::~lua_binding_class_mgr_base() {}

        lua_binding_mgr::lua_binding_mgr() {}

        lua_binding_mgr::~lua_binding_mgr() {}

        int lua_binding_mgr::add_lua_engine(lua_engine *engine) {
            if (NULL == engine || NULL == engine->get_lua_state()) {
                return -1;
            }

            for (func_type &fn : auto_bind_list_) {
                lua_auto_block block(engine->get_lua_state());
                fn(engine->get_lua_state());
            }

            for (auto &cmgr : lua_states_) {
                cmgr->add_lua_state(engine->get_lua_state());
            }

            return 0;
        }

        int lua_binding_mgr::remove_lua_engine(lua_engine *engine) {
            if (NULL == engine || NULL == engine->get_lua_state()) {
                return -1;
            }

            for (auto &cmgr : lua_states_) {
                cmgr->remove_lua_state(engine->get_lua_state());
            }

            return 0;
        }

        int lua_binding_mgr::proc(lua_engine *engine) {
            lua_State* L = NULL;
            if (NULL != engine) {
                L = engine->get_lua_state();
            }

            for (auto &mgr : lua_states_) {
                mgr->proc(L);
            }

            return 0;
        }

        void lua_binding_mgr::add_bind(func_type fn) { auto_bind_list_.push_back(fn); }

        lua_binding_wrapper::lua_binding_wrapper(lua_binding_mgr::func_type fn) { lua_binding_mgr::me()->add_bind(fn); }

        lua_binding_wrapper::~lua_binding_wrapper() {}
    }
}
