#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <list>
#include <sstream>

#include <common/string_oprs.h>

#include "lua_profile.h"

extern "C" {
#include "lualib.h"
}

namespace script {
    namespace lua {
        static int lua_profile_start(lua_State *L) {
            lua_profile::instance()->init(L);
            return 0;
        }

        static int lua_profile_stop(lua_State *L) {
            lua_profile::instance()->stop(L);
            return 0;
        }

        static int lua_profile_reset(lua_State *) {
            lua_profile::instance()->reset();
            return 0;
        }

        static int lua_profile_enable(lua_State *) {
            lua_profile::instance()->enable();
            return 0;
        }

        static int lua_profile_disable(lua_State *) {
            lua_profile::instance()->disable();
            return 0;
        }

        static int lua_profile_enable_native_prof(lua_State *) {
            lua_profile::instance()->enable_native_prof();
            return 0;
        }

        static int lua_profile_disable_native_prof(lua_State *) {
            lua_profile::instance()->disable_native_prof();
            return 0;
        }

        static int lua_profile_dump_to(lua_State *L) {
            std::string file_path;
            if (lua_gettop(L) > 0) {
                size_t len = 0;
                const char *fn = luaL_checklstring(L, 1, &len);
                file_path.assign(fn, len);
            }

            std::string ret = lua_profile::instance()->dump_to(file_path);
            lua_pushlstring(L, ret.c_str(), ret.size());
            return 1;
        }

        static int lua_profile_dump(lua_State *L) {
            std::string ret = lua_profile::instance()->dump();
            lua_pushlstring(L, ret.c_str(), ret.size());
            return 1;
        }

        static int lua_profile_hook_run_fn_gen_stats_table(lua_State *L, lua_profile_stack_data::stack_ptr_t root_ptr) {
            lua_createtable(L, 0, 8);

            lua_pushstring(L, root_ptr->source.c_str());
            lua_setfield(L, -2, "source");

            lua_pushstring(L, root_ptr->name.c_str());
            lua_setfield(L, -2, "name");

            lua_pushinteger(L, root_ptr->line_number);
            lua_setfield(L, -2, "line_number");

            lua_pushinteger(
                L, static_cast<lua_Integer>(std::chrono::duration_cast<std::chrono::microseconds>(root_ptr->call_full_duration).count()));
            lua_setfield(L, -2, "call_full_duration");

            lua_pushinteger(
                L, static_cast<lua_Integer>(std::chrono::duration_cast<std::chrono::microseconds>(root_ptr->call_inner_duration).count()));
            lua_setfield(L, -2, "call_inner_duration");

            lua_pushinteger(
                L, static_cast<lua_Integer>(std::chrono::duration_cast<std::chrono::microseconds>(root_ptr->call_native_duration).count()));
            lua_setfield(L, -2, "call_native_duration");

            lua_pushinteger(L, root_ptr->call_count);
            lua_setfield(L, -2, "call_count");

            lua_createtable(L, 0, static_cast<int>(root_ptr->children.size()));

            int index = 1;
            for (auto child_ptr : root_ptr->children) {
                lua_pushinteger(L, index);
                lua_profile_hook_run_fn_gen_stats_table(L, child_ptr.second);
                lua_settable(L, -3);
                ++index;
            }
            lua_setfield(L, -2, "children");

            return 1;
        }

        // hook_fn[lua fn, stat fn](...)
        static int lua_profile_hook_run_fn(lua_State *L) {
            auto profile = lua_profile::instance();
            // 获取Lua调用信息
            lua_Debug ar;
            lua_getstack(L, 0, &ar);
            lua_getinfo(L, "Sn", &ar);

            lua_profile_stack_data::stack_ptr_t root_ptr = lua_profile_stack_data::make(std::make_pair(__FUNCTION__, __LINE__));
            root_ptr->name = ar.name;
            size_t top = profile->push_fn(root_ptr);

            // native 级cpu时钟统计
            auto start_tm = lua_profile_stack_data::clock_t::now();

            // 转入原来的lua 函数
            int param_num = lua_gettop(L);
            lua_pushvalue(L, lua_upvalueindex(1));

            for (int i = 1; i <= param_num; ++i) {
                lua_pushvalue(L, i);
            }
            lua_call(L, param_num, LUA_MULTRET);

            auto duration = lua_profile_stack_data::clock_t::now() - start_tm;
            // 关闭统计
            profile->pop_fn(top);

            // 统计函数
            {
                lua_pushvalue(L, lua_upvalueindex(2));
                if (lua_isnil(L, -1)) {
                    lua_pop(L, 1);
                } else {
                    lua_profile_hook_run_fn_gen_stats_table(L, root_ptr);
                    lua_call(L, 1, 0);
                }

                ++root_ptr->call_count;
                root_ptr->call_full_duration += duration;
            }

            // 优先读取第一子节点的信息
            if (!root_ptr->children.empty()) {
                root_ptr->source = root_ptr->children.begin()->second->source;
                root_ptr->line_number = root_ptr->children.begin()->second->line_number;
            }

            // TODO hook 的函数级监控
            int ret_top = lua_gettop(L);
            return ret_top - param_num;
        }

        // recovery_fn[table, name, old fn](...)
        static int lua_profile_hook_recovery_fn(lua_State *L) {
            lua_pushvalue(L, lua_upvalueindex(2));
            lua_pushvalue(L, lua_upvalueindex(3));
            lua_settable(L, lua_upvalueindex(1));
            return 0;
        }

        static int lua_profile_hook(lua_State *L) {
            // recovery_fn: profile.hook(table, name, stat_fn)
            int param_num = lua_gettop(L);
            if (param_num < 2) {
                fprintf(stderr, "profile.hook(table, field name[, stats function]) require at least 2 parameters");
                return 0;
            }

            if (0 == lua_istable(L, 1)) {
                fprintf(stderr, "profile.hook(table, field name[, stats function]) require parameter 1 to be a lua table");
                return 0;
            }

            if (0 == lua_isstring(L, 2) && 0 == lua_isnumber(L, 2)) {
                fprintf(stderr, "profile.hook(table, field name[, stats function]) require parameter 2 to be a string or number");
                return 0;
            }

            // get old table fn
            lua_pushvalue(L, 2);
            lua_gettable(L, 1);
            if (!(lua_isfunction(L, param_num + 1) && !lua_iscfunction(L, param_num + 1))) {
                fprintf(stderr, "profile.hook(table, field[, stats function]) require table.field to be a lua function");
                lua_pop(L, 1);
                return 0;
            }

            // table.key
            lua_pushvalue(L, 2);

            // lua fn, stat fn
            lua_pushvalue(L, param_num + 1);
            if (param_num > 2) {
                lua_pushvalue(L, 3);
            } else {
                lua_pushnil(L);
            }
            lua_pushcclosure(L, lua_profile_hook_run_fn, 2);
            lua_settable(L, 1);


            // recovery_fn[table, name, old fn]()
            lua_pushvalue(L, 1);
            lua_pushvalue(L, 2);
            lua_pushvalue(L, param_num + 1);
            lua_pushcclosure(L, lua_profile_hook_recovery_fn, 3);

            // remove old table fn
            lua_remove(L, -2);
            return 1;
        }

        int lua_profile_openlib(lua_State *L) {
            int top = lua_gettop(L);

            luaL_Reg lib_funcs[] = {{"start", lua_profile_start},
                                    {"stop", lua_profile_stop},
                                    {"reset", lua_profile_reset},
                                    {"enable", lua_profile_enable},
                                    {"disable", lua_profile_disable},
                                    {"enable_native_prof", lua_profile_enable_native_prof},
                                    {"disable_native_prof", lua_profile_disable_native_prof},
                                    {"dump_to", lua_profile_dump_to},
                                    {"dump", lua_profile_dump},
                                    {"hook", lua_profile_hook},
                                    {NULL, NULL}};

#if LUA_VERSION_NUM <= 501
            luaL_register(L, "profile", lib_funcs);
#else
            luaL_newlib(L, lib_funcs);
            lua_setglobal(L, "profile");
#endif

            lua_settop(L, top);
            return 0;
        }

        void lua_profile_stat_data::reset() {
            call_count = 0;
            call_full_duration = call_full_duration.zero();
            call_inner_duration = call_inner_duration.zero();
            call_native_duration = call_native_duration.zero();
        }


        lua_profile_stack_data::stack_ptr_t lua_profile_stack_data::make(const prof_key_t &key) {
            stack_ptr_t ret = stack_ptr_t(new lua_profile_stack_data());
            ret->line_number = key.second;
            ret->source = key.first;
            ret->call_count = 0;
            ret->call_full_duration = clock_t::duration::zero();
            ret->call_inner_duration = clock_t::duration::zero();
            ret->call_native_duration = clock_t::duration::zero();

            return ret;
        }

        lua_profile_stack_data::stack_ptr_t lua_profile_stack_data::enter_fn(stack_ptr_t &self, const prof_key_t &key) {
            assert(self.get());

            auto iter = self->children.find(key);
            if (iter != self->children.end()) {
                self = iter->second;
                return self;
            }

            stack_ptr_t ret = make(key);
            self->children[key] = ret;
            ret->parent = self;

            self = ret;

            return self;
        }

        lua_profile_stack_data::stack_ptr_t lua_profile_stack_data::exit_fn(stack_ptr_t &self) {
            assert(self.get());

            ++self->call_count;
            self = self->parent.lock();

            return self;
        }


        lua_profile::lua_profile() : inited_(false), enabled_(true), origin_hook_(NULL), origin_mask_(0) {}


        lua_profile::~lua_profile() {}

        void lua_profile::init(lua_State *L) {
            if (inited_) return;

            reset();

            origin_hook_ = lua_gethook(L);
            origin_mask_ = lua_gethookmask(L);
            int mask = origin_mask_ | LUA_MASKCALL | LUA_MASKRET;
            lua_sethook(L, Hook_Fn, mask, lua_gethookcount(L));
        }

        void lua_profile::reset() {
            // 初始化
            call_stack_.reserve(1024);
            call_fn_prof_list_.reserve(1024);
            call_stack_.clear();
            call_stats_.clear();

            lua_profile_call_data &call_data = enter_lua_func(prof_key_t("#root", 0));
            call_data.call_stats->name = "@root";

            enabled_native_code_ = true;
        }

        void lua_profile::stop(lua_State *L) {
            if (!inited_) return;

            inited_ = false;

            lua_sethook(L, origin_hook_, origin_mask_, lua_gethookcount(L));
            origin_hook_ = NULL;
            origin_mask_ = 0;
        }

        void lua_profile::enable() { enabled_ = true; }

        void lua_profile::disable() { enabled_ = false; }

        void lua_profile::enable_native_prof() { enabled_native_code_ = true; }

        void lua_profile::disable_native_prof() { enabled_native_code_ = false; }

        std::string lua_profile::dump_to(const std::string &file_path) {
            std::fstream f;
            std::stringstream ss;

            std::ostream *out = NULL;
            if (!file_path.empty()) {
                f.open(file_path.c_str(), std::ios::out);
            }

            if (f.is_open()) {
                out = &f;
            } else {
                out = &ss;
            }


            (*out) << "File Name, Line, Function Name, Call Times, Full Call Clock(Total), Full Call Clock(Avg.), Inner Call Clock(Total), "
                      "Inner Call Clock(Avg.), Native Call Clock(Total), Native Call Clock(Avg.)"
                   << std::endl;
            for (map_t::value_type &ele : call_stats_) {
                (*out) << ele.second->source << ", " << ele.second->line_number << ", " << ele.second->name << ", "
                       << ele.second->call_count << ", " << ele.second->call_full_duration.count() << ", "
                       << (ele.second->call_full_duration.count() / ele.second->call_count) << ", "
                       << ele.second->call_inner_duration.count() << ", "
                       << (ele.second->call_inner_duration.count() / ele.second->call_count) << ", "
                       << ele.second->call_native_duration.count() << ", "
                       << (ele.second->call_native_duration.count() / ele.second->call_count) << std::endl;
            }

            return ss.str();
        }

        std::string lua_profile::dump() { return dump_to(std::string()); }

        size_t lua_profile::push_fn(lua_profile_stack_data::stack_ptr_t ptr) {
            size_t ret = call_fn_prof_list_.size();
            call_fn_prof_list_.push_back(ptr);
            return ret;
        }

        void lua_profile::pop_fn(size_t p) {
            while (call_fn_prof_list_.size() > p) call_fn_prof_list_.pop_back();

            // while (call_fn_prof_list_.size() > p) {
            //    while (call_fn_prof_list_.size() > p && call_fn_prof_list_.back()->parent.expired())
            //        call_fn_prof_list_.pop_back();

            //    // 存在未过期函数，主动退出
            //    // 解决由于lua或者luajit某些函数不会触发 hook 的BUG
            //    if (call_fn_prof_list_.size() > p && !call_fn_prof_list_.back()->parent.expired()) {
            //        auto this_stack = &call_stack_.back();

            //        // shit, 为毛线本地调用TMD没有return的hook?
            //        if (this_stack->is_native_call) {
            //            exit_native_func();
            //        } else {
            //            exit_lua_func();
            //        }
            //    }
            //}
        }

        void lua_profile::Hook_Fn(lua_State *L, lua_Debug *ar) {
            lua_profile *profile = lua_profile::instance();
            lua_Hook origin_hook = profile->origin_hook_;
            if (NULL != origin_hook && ((1 << ar->event) & profile->origin_mask_)) {
                origin_hook(L, ar);
            }

            if (!profile->enabled_) {
                return;
            }

            if (LUA_HOOKCALL != ar->event && LUA_HOOKRET != ar->event) {
                return;
            }

            ar->name = NULL;
            //#ifdef LUA_HOOKTAILRET
            //            if (LUA_HOOKTAILRET == ar->event) {
            //                assert(false);
            //            } else {
            //#elif defined(LUA_HOOKTAILCALL)
            //            if (LUA_HOOKTAILCALL == ar->event) {
            //                assert(false);
            //            } else {
            //#endif
            lua_getinfo(L, "Sn", ar);
            //#if defined(LUA_HOOKTAILRET) || defined(LUA_HOOKTAILCALL)
            //            }
            //#endif


            bool lua_func = (0 == UTIL_STRFUNC_STRCASE_CMP("Lua", ar->what)) || (0 == ar->what[0]);
            bool lua_is_main = !lua_func && (0 == UTIL_STRFUNC_STRCASE_CMP("main", ar->what));
            bool native_func = (!lua_func) && (!lua_is_main) && (0 == UTIL_STRFUNC_STRCASE_CMP("C", ar->what));

            prof_key_t index_key;
            if (lua_func) {
                index_key = prof_key_t(ar->source, ar->linedefined);
            } else if (lua_is_main) {
                index_key = prof_key_t(std::string("main:") + ar->source, ar->linedefined);
            } else if (native_func && profile->enabled_native_code_) {
                char name[256] = {0};
                if (0 == UTIL_STRFUNC_STRCASE_CMP("method", ar->namewhat)) {
                    UTIL_STRFUNC_SNPRINTF(name, sizeof(name), "%s.%s", "[native object]", ar->name);
                } else {
                    UTIL_STRFUNC_SNPRINTF(name, sizeof(name), "%s.%s", ar->namewhat, ar->name);
                }

                index_key = prof_key_t(name, ar->linedefined);
            }

            // if (!native_func && NULL != ar->name && 0 == UTIL_STRFUNC_STRCASE_CMP("alloc_id", ar->name)) {
            //     int k = 0;
            // }

            // 开始调用函数统计
            if (LUA_HOOKCALL == ar->event) {
                if (lua_func || lua_is_main) {
                    lua_profile_call_data &this_stack = profile->enter_lua_func(index_key);
                    if (this_stack.call_stats->name.empty() && NULL != ar->name && ar->name[0]) {
                        if (0 == UTIL_STRFUNC_STRCASE_CMP("field", ar->namewhat)) {
                            // this_stack.call_stats->name = "[anonymous function]";
                            // 有可能是匿名函数, 而且更扯蛋的是，有时候不会触发LUA_MASKRET
                            this_stack.call_stats->name = ar->name;
                        } else {
                            this_stack.call_stats->name = ar->name;
                        }
                    }
                } else if (native_func && profile->enabled_native_code_) {
                    char name[256] = {0};
                    if (0 == UTIL_STRFUNC_STRCASE_CMP("method", ar->namewhat)) {
                        UTIL_STRFUNC_SNPRINTF(name, sizeof(name), "%s.%s", "[native object]", ar->name);
                    } else {
                        UTIL_STRFUNC_SNPRINTF(name, sizeof(name), "%s.%s", ar->namewhat, ar->name);
                    }

                    lua_profile_call_data &this_stack = profile->enter_native_func(index_key);
                    if (this_stack.call_stats->name.empty()) {
                        this_stack.call_stats->name = "[native code]";
                    }
                }

                // [文件名] ar->source
                // [起始行] ar->linedefined (C函数返回值-1)
                // [函数名/变量名] ar->name
                // [名字类型] ar->namewhat  : field->匿名函数 , method->具名函数 , global->系统函数, local->本地C函数
                // [类型] ar->what Lua|C|main
            } else {  // 结束调用函数统计
                if (lua_func || lua_is_main) {
                    profile->exit_lua_func(index_key);
                } else {
                    char name[256] = {0};
                    if (0 == UTIL_STRFUNC_STRCASE_CMP("method", ar->namewhat)) {
                        UTIL_STRFUNC_SNPRINTF(name, sizeof(name), "%s.%s", "[native object]", ar->name);
                    } else if (native_func && profile->enabled_native_code_) {
                        UTIL_STRFUNC_SNPRINTF(name, sizeof(name), "%s.%s", ar->namewhat, ar->name);
                    }

                    if (name[0]) {
                        profile->exit_native_func(index_key);
                    }
                }
            }
        }

        lua_profile_call_data &lua_profile::enter_lua_func(const prof_key_t &key) {
            auto iter = call_stats_.find(key);
            prof_ptr prof_data;
            if (iter == call_stats_.end()) {
                prof_data = std::make_shared<prof_ptr::element_type>();
                call_stats_[key] = prof_data;
                prof_data->reset();

                prof_data->source = key.first;
                prof_data->line_number = key.second;
            } else {
                prof_data = iter->second;
            }

            ++prof_data->call_count;

            call_stack_.push_back(lua_profile_call_data());
            lua_profile_call_data &this_stack = call_stack_.back();
            this_stack.key = key;
            this_stack.call_enter_time_point = lua_profile_call_data::clock_t::now();
            this_stack.is_native_call = false;

            this_stack.call_stats = prof_data;

            if (call_stack_.size() > 1) {
                lua_profile_call_data &pre_stack = call_stack_[call_stack_.size() - 2];
                auto duration = this_stack.call_enter_time_point - pre_stack.call_enter_time_point;
                pre_stack.call_full_duration += duration;
                pre_stack.call_inner_duration += duration;
            }


            // 函数级统计
            for (lua_profile_stack_data::stack_ptr_t &fn_prof_ptr : call_fn_prof_list_) {
                lua_profile_stack_data::enter_fn(fn_prof_ptr, key);
                if (fn_prof_ptr->name.empty() && !prof_data->name.empty()) {
                    fn_prof_ptr->name = prof_data->name;
                }
            }
            return this_stack;
        }

        void lua_profile::exit_lua_func(const prof_key_t &key) {
            lua_profile_call_data *this_stack = NULL;
            while (true) {
                // 刚启动时可能lua执行栈并不在顶层
                if (1 == call_stack_.size()) {
                    return;
                }

                this_stack = &call_stack_.back();

                if (this_stack->key != key) {
                    if (this_stack->is_native_call) {
                        exit_native_func(this_stack->key);
                    } else {
                        exit_lua_func(this_stack->key);
                    }
                    continue;
                }
                break;
            }

            auto now_clock = lua_profile_call_data::clock_t::now();
            auto duration = now_clock - this_stack->call_enter_time_point;
            this_stack->call_full_duration += duration;
            this_stack->call_inner_duration += duration;

            // 上一层的总调用时间加子节点调用时间
            lua_profile_call_data &pre_stack = call_stack_[call_stack_.size() - 2];
            pre_stack.call_full_duration += this_stack->call_full_duration;
            // 上一层更新新起始时间
            pre_stack.call_enter_time_point = now_clock;


            // 归档本次调用统计
            this_stack->call_stats->call_full_duration += this_stack->call_full_duration;
            this_stack->call_stats->call_inner_duration += this_stack->call_inner_duration;
            ++this_stack->call_stats->call_count;


            // 函数级统计
            for (lua_profile_stack_data::stack_ptr_t &fn_prof_ptr : call_fn_prof_list_) {
                fn_prof_ptr->call_full_duration += this_stack->call_full_duration;
                fn_prof_ptr->call_inner_duration += this_stack->call_inner_duration;
                lua_profile_stack_data::exit_fn(fn_prof_ptr);
            }

            // 数据清理
            call_stack_.pop_back();
            this_stack = NULL;
        }

        lua_profile_call_data &lua_profile::enter_native_func(const prof_key_t &key) {
            lua_profile_call_data &ret = enter_lua_func(key);
            ret.is_native_call = true;
            return ret;
        }

        void lua_profile::exit_native_func(const prof_key_t &key) {
            lua_profile_call_data *this_stack = NULL;
            while (true) {
                // 刚启动时可能lua执行栈并不在顶层
                if (1 == call_stack_.size()) {
                    return;
                }

                this_stack = &call_stack_.back();

                if (this_stack->key != key) {
                    if (this_stack->is_native_call) {
                        exit_native_func(this_stack->key);
                    } else {
                        exit_lua_func(this_stack->key);
                    }
                    continue;
                }
                break;
            }
            assert(this_stack->is_native_call);

            auto now_clock = lua_profile_call_data::clock_t::now();
            auto duration = now_clock - this_stack->call_enter_time_point;
            this_stack->call_full_duration += duration;
            this_stack->call_inner_duration += duration;

            // 上一层的总调用时间加子节点调用时间
            lua_profile_call_data &pre_stack = call_stack_[call_stack_.size() - 2];
            pre_stack.call_full_duration += this_stack->call_full_duration;
            // native code 没有额外统计，所以可以直接加
            pre_stack.call_stats->call_native_duration += this_stack->call_full_duration;
            // 上一层更新新起始时间
            pre_stack.call_enter_time_point = now_clock;


            // 归档本次调用统计
            this_stack->call_stats->call_full_duration += this_stack->call_full_duration;
            this_stack->call_stats->call_inner_duration += this_stack->call_inner_duration;
            ++this_stack->call_stats->call_count;


            // 函数级统计
            for (lua_profile_stack_data::stack_ptr_t &fn_prof_ptr : call_fn_prof_list_) {
                fn_prof_ptr->call_full_duration += this_stack->call_full_duration;
                fn_prof_ptr->call_inner_duration += this_stack->call_inner_duration;

                lua_profile_stack_data::exit_fn(fn_prof_ptr);
                fn_prof_ptr->call_native_duration += this_stack->call_inner_duration;  // 父节点追加本地调用时间
            }


            // 数据清理
            call_stack_.pop_back();
            this_stack = NULL;
        }
    }  // namespace lua
}  // namespace script
