#ifndef SCRIPT_LUA_LUABINDINGWRAPPER
#define SCRIPT_LUA_LUABINDINGWRAPPER

#pragma once

#include <array>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <typeinfo>
#include <vector>

#include <std/explicit_declare.h>

#include "lua_binding_mgr.h"
#include "lua_binding_utils.h"


#ifdef max
#undef max
#endif

namespace script {
    namespace lua {
        /*************************************\
        |*  以下是一些拓展类型，用于一系列优化目的   *|
        \*************************************/

        struct string_buffer {
            typedef std::string::value_type value_type;
            const value_type *data;
            size_t length;

            string_buffer(const value_type *d) : data(d), length(strlen(d) + 1) {}
            string_buffer(const value_type *d, size_t s) : data(d), length(s) {}
        };

        /*************************************\
        |*  以上是一些拓展类型，用于一系列优化目的   *|
        \*************************************/

        namespace detail {

            /*************************************\
            |* 以下type_traits用于枚举动态模板参数下标 *|
            \*************************************/

            template <int... _index>
            struct index_seq_list {
                typedef index_seq_list<_index..., sizeof...(_index)> next_type;
            };

            template <typename... TP>
            struct build_args_index;

            template <>
            struct build_args_index<> {
                typedef index_seq_list<> index_seq_type;
            };

            template <typename TP1, typename... TP>
            struct build_args_index<TP1, TP...> {
                typedef typename build_args_index<TP...>::index_seq_type::next_type index_seq_type;
            };

            /*************************************\
            |* 以上type_traits用于枚举动态模板参数下标 *|
            \*************************************/

            template <typename Ty>
            struct wraper_var_lua_type;

            template <>
            struct wraper_var_lua_type<lua_Unsigned> {
                typedef lua_Unsigned value_type;

                template <typename TParam>
                static int wraper(lua_State *L, const TParam &v) {
                    if (sizeof(v) >= sizeof(lua_Integer) && v > static_cast<TParam>(std::numeric_limits<lua_Integer>::max())) {
                        lua_getglobal(L, "tonumber");
                        char n[32] = {0};
                        sprintf(n, "%llu", static_cast<unsigned long long>(v));
                        lua_pushstring(L, n);
                        lua_call(L, 1, 1);
                    } else {
                        lua_pushinteger(L, static_cast<lua_Unsigned>(v));
                    }

                    return 1;
                }
            };

            template <>
            struct wraper_var_lua_type<lua_CFunction> {
                typedef lua_CFunction value_type;

                template <typename TParam>
                static int wraper(lua_State *L, TParam v) {
                    lua_pushcfunction(L, static_cast<lua_CFunction>(v));
                    return 1;
                }
            };

            template <>
            struct wraper_var_lua_type<lua_Integer> {
                typedef lua_Integer value_type;

                template <typename TParam>
                static int wraper(lua_State *L, const TParam &v) {
                    lua_pushinteger(L, static_cast<lua_Integer>(v));
                    return 1;
                }
            };

            template <>
            struct wraper_var_lua_type<lua_Number> {
                typedef lua_Number value_type;

                template <typename TParam>
                static int wraper(lua_State *L, const TParam &v) {
                    lua_pushnumber(L, static_cast<lua_Number>(v));
                    return 1;
                }
            };

            template <typename Ty>
            struct wraper_ptr_var_lua_type {
                typedef Ty value_type;

                template <typename TParam>
                static int wraper(lua_State *L, TParam &&v) {
                    static_assert(std::is_pointer<TParam>::value, "parameter must be a pointer");

                    lua_pushlightuserdata(L, v);
                    return 1;
                }
            };

            template <typename Ty>
            struct wraper_var_lua_type {
                typedef Ty value_type;

                template <typename TParam>
                static int wraper(lua_State *L, TParam &&v) {
                    typedef
                        typename std::remove_reference<typename std::remove_cv<typename std::remove_pointer<Ty>::type>::type>::type obj_t;

                    // 最好是不要这么用，如果有需要再加对非POD类型的支持吧
                    static_assert(std::is_pod<obj_t>::value, "custom type must be pod type");

                    obj_t *ptr = reinterpret_cast<obj_t *>(lua_newuserdata(L, sizeof(obj_t)));
                    memcpy(ptr, &v, sizeof(v));

                    return 1;
                }
            };


            template <typename TRet, typename... Ty>
            struct wraper_var;

            template <typename... Ty>
            struct wraper_var<void, Ty...> {
                static int wraper(lua_State *, const void *) { return 0; }
            };

            template <typename... Ty>
            struct wraper_var<uint8_t, Ty...> : public wraper_var_lua_type<lua_Unsigned> {};
            template <typename... Ty>
            struct wraper_var<uint16_t, Ty...> : public wraper_var_lua_type<lua_Unsigned> {};
            template <typename... Ty>
            struct wraper_var<uint32_t, Ty...> : public wraper_var_lua_type<lua_Unsigned> {};
            template <typename... Ty>
            struct wraper_var<uint64_t, Ty...> : public wraper_var_lua_type<lua_Unsigned> {};

            template <typename... Ty>
            struct wraper_var<int8_t, Ty...> : public wraper_var_lua_type<lua_Integer> {};
            template <typename... Ty>
            struct wraper_var<int16_t, Ty...> : public wraper_var_lua_type<lua_Integer> {};
            template <typename... Ty>
            struct wraper_var<int32_t, Ty...> : public wraper_var_lua_type<lua_Integer> {};
            template <typename... Ty>
            struct wraper_var<int64_t, Ty...> : public wraper_var_lua_type<lua_Integer> {};

            template <typename... Ty>
            struct wraper_var<float, Ty...> : public wraper_var_lua_type<lua_Number> {};
            template <typename... Ty>
            struct wraper_var<double, Ty...> : public wraper_var_lua_type<lua_Number> {};

            template <typename... Ty>
            struct wraper_var<lua_CFunction, Ty...> : public wraper_var_lua_type<lua_CFunction> {};

            template <typename... Ty>
            struct wraper_var<bool, Ty...> {
                static int wraper(lua_State *L, const bool &v) {
                    lua_pushboolean(L, v ? 1 : 0);
                    return 1;
                }
            };

            template <typename... Ty>
            struct wraper_var<char *, Ty...> {
                static int wraper(lua_State *L, const char *v) {
                    lua_pushstring(L, v);
                    return 1;
                }
            };

            template <typename... Ty>
            struct wraper_var<const char *, Ty...> {
                static int wraper(lua_State *L, const char *v) {
                    lua_pushstring(L, v);
                    return 1;
                }
            };

            template <typename... Ty>
            struct wraper_var< ::script::lua::string_buffer, Ty...> {
                static int wraper(lua_State *L, const ::script::lua::string_buffer &v) {
                    lua_pushlstring(L, v.data, v.length);
                    return 1;
                }
            };

            // 注册类的打解包
            template <typename TC, typename... Ty>
            struct wraper_var<std::shared_ptr<TC>, Ty...> {
                static int wraper(lua_State *L, const std::shared_ptr<TC> &v) {
                    typedef typename lua_binding_userdata_info<TC>::userdata_type ud_t;

                    // 无效则push nil
                    if (!v) {
                        lua_pushnil(L);
                        return 1;
                    }

                    void *buff = lua_newuserdata(L, sizeof(ud_t));
                    new (buff) ud_t(v);

                    const char *class_name = lua_binding_userdata_info<TC>::get_lua_metatable_name();
                    luaL_getmetatable(L, class_name);

                    lua_setmetatable(L, -2);
                    return 1;
                }
            };

            // 注册类的打解包
            template <typename TC, typename... Ty>
            struct wraper_var<std::weak_ptr<TC>, Ty...> {
                static int wraper(lua_State *L, const std::weak_ptr<TC> &v) {
                    typedef typename lua_binding_userdata_info<TC>::userdata_type ud_t;

                    // 无效则push nil
                    if (v.expired()) {
                        lua_pushnil(L);
                        return 1;
                    }

                    void *buff = lua_newuserdata(L, sizeof(ud_t));
                    new (buff) ud_t(v);

                    const char *class_name = lua_binding_userdata_info<TC>::get_lua_metatable_name();
                    luaL_getmetatable(L, class_name);

                    lua_setmetatable(L, -2);
                    return 1;
                }
            };

            template <typename... Ty>
            struct wraper_var<std::string, Ty...> {
                static int wraper(lua_State *L, const std::string &v) {
                    lua_pushlstring(L, v.c_str(), v.size());
                    return 1;
                }
            };

            // ============== stl 扩展 =================
            template <typename TLeft, typename TRight, typename... Ty>
            struct wraper_var<std::pair<TLeft, TRight>, Ty...> {
                static int wraper(lua_State *L, const std::pair<TLeft, TRight> &v) {
                    lua_createtable(L, 2, 0);

                    lua_pushinteger(L, 1);
                    wraper_var<TLeft>::wraper(L, v.first);
                    lua_settable(L, -3);

                    lua_pushinteger(L, 2);
                    wraper_var<TRight>::wraper(L, v.second);
                    lua_settable(L, -3);

                    return 1;
                }
            };

            template <typename Ty, typename... Tl>
            struct wraper_var<std::vector<Ty>, Tl...> {
                static int wraper(lua_State *L, const std::vector<Ty> &v) {
                    lua_Unsigned res = 1;
                    lua_newtable(L);
                    int tb = lua_gettop(L);
                    for (const Ty &ele : v) {
                        // 目前只支持一个值
                        lua_pushunsigned(L, res);
                        wraper_var<Ty>::wraper(L, ele);
                        lua_settable(L, -3);

                        ++res;
                    }
                    lua_settop(L, tb);
                    return 1;
                }
            };

            template <typename Ty, typename... Tl>
            struct wraper_var<std::list<Ty>, Tl...> {
                static int wraper(lua_State *L, const std::list<Ty> &v) {
                    lua_Unsigned res = 1;
                    lua_newtable(L);
                    int tb = lua_gettop(L);
                    for (const Ty &ele : v) {
                        // 目前只支持一个值
                        lua_pushunsigned(L, res);
                        wraper_var<Ty>::wraper(L, ele);
                        lua_settable(L, -3);

                        ++res;
                    }
                    lua_settop(L, tb);
                    return 1;
                }
            };

            template <typename Ty, size_t SIZE, typename... Tl>
            struct wraper_var<std::array<Ty, SIZE>, Tl...> {
                static int wraper(lua_State *L, const std::array<Ty, SIZE> &v) {
                    lua_Unsigned res = 1;
                    lua_newtable(L);
                    int tb = lua_gettop(L);
                    for (const Ty &ele : v) {
                        // 目前只支持一个值
                        lua_pushunsigned(L, res);
                        wraper_var<Ty>::wraper(L, ele);
                        lua_settable(L, -3);

                        ++res;
                    }
                    lua_settop(L, tb);
                    return 1;
                }
            };

            // --------------- stl 扩展 ----------------

            // ================ 数组支持 ================
            template <typename Ty, size_t SIZE, typename... Tl>
            struct wraper_var<Ty[SIZE], Tl...> {
                static int wraper(lua_State *L, const Ty v[SIZE]) {
                    lua_Unsigned res = 1;
                    lua_newtable(L);
                    int tb = lua_gettop(L);
                    for (const Ty &ele : v) {
                        // 目前只支持一个值
                        lua_pushunsigned(L, res);
                        wraper_var<Ty>::wraper(L, ele);
                        lua_settable(L, -3);

                        ++res;
                    }
                    lua_settop(L, tb);
                    return 1;
                }
            };

            template <size_t SIZE, typename... Tl>
            struct wraper_var<char[SIZE], Tl...> {
                static int wraper(lua_State *L, const char v[SIZE]) {
                    lua_pushstring(L, v);
                    return 1;
                }
            };
            // -------------- 数组支持 --------------

            template <typename Ty, typename... Tl>
            struct wraper_var
                : public std::conditional<
                      std::is_enum<Ty>::value || std::is_integral<Ty>::value,
                      wraper_var_lua_type<typename std::conditional<
                          std::is_enum<Ty>::value || std::is_unsigned<Ty>::value, lua_Unsigned,
                          lua_Integer>::type>,  // 枚举类型和未识别的整数(某些编译器的size_t和uint32_t/uint64_t被判定为不同类型)
                      typename std::conditional<std::is_pointer<Ty>::value,
                                                wraper_ptr_var_lua_type<Ty>,  // 指针类型
                                                wraper_var_lua_type<Ty>       // POD类型
                                                >::type>::type {};


            struct wraper_bat_cmd {
                template <typename TupleT, int N>
                struct runner {
                    int operator()(lua_State *L, TupleT &t) {
                        typedef typename std::remove_cv<
                            typename std::remove_reference<typename std::tuple_element<N, TupleT>::type>::type>::type var_t;

                        return detail::wraper_var<var_t>::wraper(L, std::get<N>(t));
                    }
                };

                template <class TupleT, int... N>
                static int wraper_bat_count(lua_State *L, TupleT &&t, index_seq_list<N...>) {
                    std::function<int(lua_State *, TupleT &)> funcs[] = {runner<TupleT, N>()...};

                    int ret = 0;
                    for (std::function<int(lua_State *, TupleT &)> &func : funcs) {
                        ret += func(L, t);
                    }

                    return ret;
                }

                template <class TupleT>
                static int wraper_bat_count(lua_State *, TupleT &&, index_seq_list<>) {
                    return 0;
                }
            };
        }  // namespace detail

        /**
         * @brief 自动打包调用lua函数
         * @return 参数个数，如果返回值小于0则不会改变参数个数
         */
        template <typename... TParams>
        int auto_call(lua_State *L, int index, TParams &&... params) {
            int top = lua_gettop(L);
            int hmsg = script::lua::fn::get_pcall_hmsg(L);

            int fn_index = index > 0 ? index : index - 1;

            if (!lua_isfunction(L, fn_index)) {
                WLOGERROR("var in lua stack %d is not a function.", index);
                lua_pop(L, 1);
                return LUA_ERRRUN;
            }

            lua_pushvalue(L, fn_index);

            int param_num = detail::wraper_bat_cmd::wraper_bat_count(L, std::forward_as_tuple(params...),
                                                                     typename detail::build_args_index<TParams...>::index_seq_type());

            int res = lua_pcall(L, param_num, LUA_MULTRET, hmsg);
            if (res) {
                WLOGERROR("call stack %d error. ret code: %d\n%s", index, res, lua_tostring(L, -1));
                lua_settop(L, top);
            } else {
                // 移除hmsg
                lua_remove(L, hmsg);
            }

            return res ? res : (lua_gettop(L) - top);
        };

        template <typename... TParams>
        int auto_call(lua_State *L, const std::string &path, TParams &&... params) {
            int top = lua_gettop(L);
            int hmsg = script::lua::fn::get_pcall_hmsg(L);

            fn::load_item(L, path);
            if (!lua_isfunction(L, -1)) {
                WLOGERROR("var in lua stack %s is not a function.", path.c_str());
                lua_settop(L, top);
                return LUA_ERRRUN;
            }

            int param_num = detail::wraper_bat_cmd::wraper_bat_count(L, std::forward_as_tuple(params...),
                                                                     typename detail::build_args_index<TParams...>::index_seq_type());

            int res = lua_pcall(L, param_num, LUA_MULTRET, hmsg);
            if (res) {
                WLOGERROR("call stack %s error. ret code: %d\n%s", path.c_str(), res, lua_tostring(L, -1));
                lua_settop(L, top);
            } else {
                // 移除hmsg
                lua_remove(L, hmsg);
            }

            return res ? res : (lua_gettop(L) - top);
        };
    }  // namespace lua
}  // namespace script
#endif
