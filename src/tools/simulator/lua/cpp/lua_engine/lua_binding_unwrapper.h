#ifndef SCRIPT_LUA_LUABINDINGUNWRAPPER
#define SCRIPT_LUA_LUABINDINGUNWRAPPER

#pragma once

#include <stdint.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>

#include <std/explicit_declare.h>

#include "../lua_module/lua_adaptor.h"
#include "lua_binding_wrapper.h"

#define LUA_CHECK_TYPE_AND_RET(name, L, index, ret)                                            \
    if (!lua_is##name(L, index)) {                                                             \
        WLOGERROR("parameter %d must be a %s, got %s", index, #name, luaL_typename(L, index)); \
        fn::print_traceback(L, "");                                                            \
        return ret;                                                                            \
    }

#define LUA_CHECK_TYPE_AND_NORET(name, L, index)                                               \
    if (!lua_is##name(L, index)) {                                                             \
        WLOGERROR("parameter %d must be a %s, got %s", index, #name, luaL_typename(L, index)); \
        fn::print_traceback(L, "");                                                            \
        return;                                                                                \
    }

namespace script {
    namespace lua {
        namespace detail {

            template <typename Tt, typename Ty>
            struct unwraper_var_lua_type;

            template <typename Tt>
            struct unwraper_var_lua_type<Tt, lua_Unsigned> {
                typedef lua_Unsigned value_type;

                static Tt unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return static_cast<Tt>(0);

                    LUA_CHECK_TYPE_AND_RET(number, L, index, static_cast<Tt>(0));
                    return static_cast<Tt>(lua_tointeger(L, index));
                }
            };

            template <typename Tt>
            struct unwraper_var_lua_type<Tt, lua_CFunction> {
                typedef lua_CFunction value_type;

                static Tt unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return static_cast<Tt>(NULL);

                    return static_cast<Tt>(lua_tocfunction(L, index));
                }
            };

            template <typename Tt>
            struct unwraper_var_lua_type<Tt, lua_Integer> {
                typedef lua_Integer value_type;

                static Tt unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return static_cast<Tt>(0);

                    LUA_CHECK_TYPE_AND_RET(number, L, index, static_cast<Tt>(0));

                    return static_cast<Tt>(lua_tointeger(L, index));
                }
            };

            template <typename Tt>
            struct unwraper_var_lua_type<Tt, lua_Number> {
                typedef lua_Number value_type;

                static Tt unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return static_cast<Tt>(0);

                    LUA_CHECK_TYPE_AND_RET(number, L, index, static_cast<Tt>(0));

                    return static_cast<Tt>(lua_tonumber(L, index));
                }
            };

            template <typename Tt, typename Ty>
            struct unwraper_ptr_var_lua_type {
                typedef Ty value_type;

                static Tt unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return NULL;

                    if (lua_isnil(L, index)) {
                        return NULL;
                    }

                    if (!lua_islightuserdata(L, index)) {
                        return NULL;
                    }

                    return reinterpret_cast<Tt>(lua_topointer(L, index));
                }
            };

            template <typename Tt, typename Ty>
            struct unwraper_var_lua_type {
                typedef Ty value_type;

                static Tt unwraper(lua_State *L, int index) {
                    typedef
                        typename std::remove_reference<typename std::remove_cv<typename std::remove_pointer<Tt>::type>::type>::type obj_t;

                    // 最好是不要这么用，如果有需要再加对非POD类型的支持吧
                    static_assert(std::is_pod<obj_t>::value, "custom type must be pod type");

                    if (lua_gettop(L) < index) {
                        return static_cast<Tt>(0);  // NULL static_cast from 'null_ptr' to 'unsigned long' is not allowed
                    }
                    if (lua_isnil(L, index)) {
                        return static_cast<Tt>(0);  // NULL static_cast from 'null_ptr' to 'unsigned long' is not allowed
                    }

                    obj_t *ptr = reinterpret_cast<obj_t *>(lua_touserdata(L, index));
                    if (NULL == ptr) {
                        return static_cast<Tt>(0);  // NULL static_cast from 'null_ptr' to 'unsigned long' is not allowed
                    }

                    return static_cast<Tt>(*ptr);
                }
            };


            template <typename TRet, typename... Ty>
            struct unwraper_var;

            template <typename... Ty>
            struct unwraper_var<void, Ty...> {
                template <typename TParam>
                static void unwraper(lua_State *, int) {}
            };

            template <typename... Ty>
            struct unwraper_var<uint8_t, Ty...> : public unwraper_var_lua_type<uint8_t, lua_Unsigned> {};
            template <typename... Ty>
            struct unwraper_var<uint16_t, Ty...> : public unwraper_var_lua_type<uint16_t, lua_Unsigned> {};
            template <typename... Ty>
            struct unwraper_var<uint32_t, Ty...> : public unwraper_var_lua_type<uint32_t, lua_Unsigned> {};
            template <typename... Ty>
            struct unwraper_var<uint64_t, Ty...> : public unwraper_var_lua_type<uint64_t, lua_Unsigned> {};

            template <typename... Ty>
            struct unwraper_var<int8_t, Ty...> : public unwraper_var_lua_type<int8_t, lua_Integer> {};
            template <typename... Ty>
            struct unwraper_var<int16_t, Ty...> : public unwraper_var_lua_type<int16_t, lua_Integer> {};
            template <typename... Ty>
            struct unwraper_var<int32_t, Ty...> : public unwraper_var_lua_type<int32_t, lua_Integer> {};
            template <typename... Ty>
            struct unwraper_var<int64_t, Ty...> : public unwraper_var_lua_type<int64_t, lua_Integer> {};

            template <typename... Ty>
            struct unwraper_var<float, Ty...> : public unwraper_var_lua_type<float, lua_Number> {};
            template <typename... Ty>
            struct unwraper_var<double, Ty...> : public unwraper_var_lua_type<double, lua_Number> {};

            template <typename... Ty>
            struct unwraper_var<lua_CFunction, Ty...> : public unwraper_var_lua_type<lua_CFunction, lua_CFunction> {};

            template <typename... Ty>
            struct unwraper_var<bool, Ty...> {
                static bool unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return static_cast<bool>(false);

                    return !!lua_toboolean(L, index);
                }
            };

            template <typename... Ty>
            struct unwraper_var<const char *, Ty...> {
                static const char *unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return static_cast<const char *>(NULL);

                    LUA_CHECK_TYPE_AND_RET(string, L, index, NULL);

                    return lua_tostring(L, index);
                }
            };

            template <typename... Ty>
            struct unwraper_var<char *, Ty...> {
                static char *unwraper(lua_State *L, int index) {
                    if (lua_gettop(L) < index) return static_cast<char *>(NULL);

                    LUA_CHECK_TYPE_AND_RET(string, L, index, NULL);

                    return const_cast<char *>(lua_tostring(L, index));
                }
            };

            template <typename... Ty>
            struct unwraper_var< ::script::lua::string_buffer, Ty...> {
                static ::script::lua::string_buffer unwraper(lua_State *L, int index) {
                    ::script::lua::string_buffer ret(NULL, 0);

                    LUA_CHECK_TYPE_AND_RET(string, L, index, ret);

                    ret.data = lua_tolstring(L, index, &ret.length);
                    return ret;
                }
            };

            // 注册类的打解包
            template <typename TC, typename... Ty>
            struct unwraper_var<std::shared_ptr<TC>, Ty...> {
                static std::shared_ptr<TC> unwraper(lua_State *L, int index) {
                    typedef typename lua_binding_userdata_info<TC>::userdata_type ud_t;

                    LUA_CHECK_TYPE_AND_RET(userdata, L, index, std::shared_ptr<TC>());
                    const char *class_name = lua_binding_userdata_info<TC>::get_lua_metatable_name();
                    ud_t *watcher = static_cast<ud_t *>(luaL_checkudata(L, index, class_name));

                    if (NULL == watcher) {
                        return std::shared_ptr<TC>();
                    }

                    return watcher->lock();
                }
            };

            // 注册类的打解包
            template <typename TC, typename... Ty>
            struct unwraper_var<std::weak_ptr<TC>, Ty...> {
                static std::weak_ptr<TC> unwraper(lua_State *L, int index) {
                    typedef typename lua_binding_userdata_info<TC>::userdata_type ud_t;

                    LUA_CHECK_TYPE_AND_RET(userdata, L, index, std::weak_ptr<TC>());

                    const char *class_name = lua_binding_userdata_info<TC>::get_lua_metatable_name();
                    ud_t *watcher = static_cast<ud_t *>(luaL_checkudata(L, index, class_name));

                    if (NULL == watcher) {
                        return std::weak_ptr<TC>();
                    }

                    return watcher;
                }
            };

            // ============== stl 扩展 =================
            template <typename TLeft, typename TRight, typename... Ty>
            struct unwraper_var<std::pair<TLeft, TRight>, Ty...> {
                static std::pair<TLeft, TRight> unwraper(lua_State *L, int index) {
                    std::pair<TLeft, TRight> ret;
                    LUA_CHECK_TYPE_AND_RET(table, L, index, ret);

                    lua_pushvalue(L, index);

                    lua_pushinteger(L, 1);
                    lua_gettable(L, -2);
                    ret.first = unwraper_var<TLeft>::unwraper(L, -1);

                    lua_pushinteger(L, 2);
                    lua_gettable(L, -3);
                    ret.second = unwraper_var<TRight>::unwraper(L, -1);

                    lua_pop(L, 3);

                    return ret;
                }
            };

            template <typename Ty, typename... Tl>
            struct unwraper_var<std::vector<Ty>, Tl...> {
                static std::vector<Ty> unwraper(lua_State *L, int index) {
                    std::vector<Ty> ret;
                    LUA_CHECK_TYPE_AND_RET(table, L, index, ret);

                    LUA_GET_TABLE_LEN(size_t len, L, index);
                    ret.reserve(len);

                    lua_pushvalue(L, index);
                    for (size_t i = 1; i <= len; ++i) {
                        lua_pushinteger(L, static_cast<lua_Unsigned>(i));
                        lua_gettable(L, -2);
                        ret.push_back(unwraper_var<Ty>::unwraper(L, -1));
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);

                    return ret;
                }
            };

            template <typename Ty, typename... Tl>
            struct unwraper_var<std::list<Ty>, Tl...> {
                static std::list<Ty> unwraper(lua_State *L, int index) {
                    std::list<Ty> ret;
                    LUA_CHECK_TYPE_AND_RET(table, L, index, ret);

                    LUA_GET_TABLE_LEN(size_t len, L, index);

                    lua_pushvalue(L, index);
                    for (size_t i = 1; i <= len; ++i) {
                        lua_pushinteger(L, static_cast<lua_Unsigned>(i));
                        lua_gettable(L, -2);
                        ret.push_back(unwraper_var<Ty>::unwraper(L, -1));
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);

                    return ret;
                }
            };

            template <typename Ty, size_t SIZE, typename... Tl>
            struct unwraper_var<std::array<Ty, SIZE>, Tl...> {
                static std::array<Ty, SIZE> unwraper(lua_State *L, int index) {
                    std::array<Ty, SIZE> ret;
                    LUA_CHECK_TYPE_AND_RET(table, L, index, ret);

                    LUA_GET_TABLE_LEN(size_t len, L, index);

                    lua_pushvalue(L, index);
                    for (size_t i = 1; i <= len && i <= SIZE; ++i) {
                        lua_pushinteger(L, static_cast<lua_Unsigned>(i));
                        lua_gettable(L, -2);
                        ret[i] = unwraper_var<Ty>::unwraper(L, -1);
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);

                    return ret;
                }
            };

            template <typename... Ty>
            struct unwraper_var<std::string, Ty...> {
                static std::string unwraper(lua_State *L, int index) {
                    std::string ret;
                    LUA_CHECK_TYPE_AND_RET(string, L, index, ret);

                    size_t len = 0;
                    const char *start = lua_tolstring(L, index, &len);
                    ret.assign(start, len);
                    return ret;
                }
            };

            // --------------- stl 扩展 ----------------

            // ================ 数组支持 ================
            template <typename Ty, size_t SIZE>
            struct unwraper_var_array_support {
                typedef Ty type[SIZE];
            };

            template <typename Ty, size_t SIZE, typename... Tl>
            struct unwraper_var<Ty[SIZE], Tl...> {
                static typename unwraper_var_array_support<Ty, SIZE>::type unwraper(lua_State *L, int index) {
                    typename unwraper_var_array_support<Ty, SIZE>::type ret;
                    LUA_CHECK_TYPE_AND_RET(table, L, index, ret);

                    LUA_GET_TABLE_LEN(size_t len, L, index);

                    lua_pushvalue(L, index);
                    for (size_t i = 1; i <= len && i <= SIZE; ++i) {
                        lua_pushinteger(L, static_cast<lua_Unsigned>(i));
                        lua_gettable(L, -2);
                        ret[i] = unwraper_var<Ty>::unwraper(L, -1);
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);

                    return ret;
                }
            };

            template <size_t SIZE, typename... Tl>
            struct unwraper_var<char[SIZE], Tl...> {
                static typename unwraper_var_array_support<char, SIZE>::type unwraper(lua_State *L, int index) {
                    typename unwraper_var_array_support<char, SIZE>::type ret = {0};
                    LUA_CHECK_TYPE_AND_RET(string, L, index, ret);

                    size_t len = 0;

                    const char *start = lua_tolstring(L, index, &len);
                    using std::copy;
                    using std::min;
                    copy(start, start + min(SIZE, len), ret);

                    return ret;
                }
            };
            // -------------- 数组支持 --------------

            template <typename Ty, typename... Tl>
            struct unwraper_var
                : public std::conditional<
                      std::is_enum<Ty>::value || std::is_integral<Ty>::value,
                      unwraper_var_lua_type<Ty, typename std::conditional<std::is_enum<Ty>::value || std::is_unsigned<Ty>::value,
                                                                          lua_Unsigned, lua_Integer>::type>,    // 枚举类型
                      typename std::conditional<std::is_pointer<Ty>::value, unwraper_ptr_var_lua_type<Ty, Ty>,  // 指针类型
                                                unwraper_var_lua_type<Ty, Ty>                                   // POD类型
                                                >::type>::type {};


            // --------------- 解包装接口结束 ----------------

            template <typename Tr>
            struct unwraper_static_fn_base;

            template <>
            struct unwraper_static_fn_base<void> {
                template <typename Tfn, class TupleT, int... N>
                static int run_fn(lua_State *L, Tfn fn, index_seq_list<N...>) {
                    fn(unwraper_var<typename std::tuple_element<N, TupleT>::type>::unwraper(L, N + 1)...);
                    return 0;
                }
            };

            template <typename Tr>
            struct unwraper_static_fn_base {
                template <typename Tfn, class TupleT, int... N>
                static int run_fn(lua_State *L, Tfn fn, index_seq_list<N...>) {
                    return wraper_var<typename std::remove_cv<typename std::remove_reference<Tr>::type>::type>::wraper(
                        L, fn(unwraper_var<typename std::tuple_element<N, TupleT>::type>::unwraper(L, N + 1)...));
                }
            };

            template <typename Tr>
            struct unwraper_static_fn_base_with_L;

            template <>
            struct unwraper_static_fn_base_with_L<void> {
                template <typename Tfn, class TupleT, int... N>
                static int run_fn(lua_State *L, Tfn fn, index_seq_list<N...>) {
                    fn(L, unwraper_var<typename std::tuple_element<N, TupleT>::type>::unwraper(L, N + 1)...);
                    return 0;
                }
            };

            template <typename Tr>
            struct unwraper_static_fn_base_with_L {
                template <typename Tfn, class TupleT, int... N>
                static int run_fn(lua_State *L, Tfn fn, index_seq_list<N...>) {
                    return wraper_var<typename std::remove_cv<typename std::remove_reference<Tr>::type>::type>::wraper(
                        L, fn(L, unwraper_var<typename std::tuple_element<N, TupleT>::type>::unwraper(L, N + 1)...));
                }
            };

            /*************************************\
            |* 静态函数Lua绑定，动态参数个数          *|
            \*************************************/
            template <typename Tr, typename... TParam>
            struct unwraper_static_fn;

            template <typename Tr, typename... TParam>
            struct unwraper_static_fn<Tr, lua_State *, TParam...> : public unwraper_static_fn_base_with_L<Tr> {
                typedef unwraper_static_fn_base_with_L<Tr> base_type;
                typedef Tr (*value_type)(lua_State *, TParam...);


                // 动态参数个数
                static int LuaCFunction(lua_State *L) {
                    value_type fn = reinterpret_cast<value_type>(lua_touserdata(L, lua_upvalueindex(1)));
                    if (NULL == fn) {
                        // 找不到函数
                        return 0;
                    }

                    return base_type::template run_fn<
                        value_type, std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, fn, typename build_args_index<TParam...>::index_seq_type());
                }
            };

            template <typename Tr, typename... TParam>
            struct unwraper_static_fn : public unwraper_static_fn_base<Tr> {
                typedef unwraper_static_fn_base<Tr> base_type;
                typedef Tr (*value_type)(TParam...);


                // 动态参数个数
                static int LuaCFunction(lua_State *L) {
                    value_type fn = reinterpret_cast<value_type>(lua_touserdata(L, lua_upvalueindex(1)));
                    if (NULL == fn) {
                        // 找不到函数
                        return 0;
                    }

                    return base_type::template run_fn<
                        value_type, std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, fn, typename build_args_index<TParam...>::index_seq_type());
                }
            };

            /*************************************\
            |* 仿函数Lua绑定，动态参数个数          *|
            \*************************************/
            template <typename Tr, typename... TParam>
            struct unwraper_functor_fn;

            template <typename Tr, typename... TParam>
            struct unwraper_functor_fn<Tr, lua_State *, TParam...> : public unwraper_static_fn_base_with_L<Tr> {
                typedef unwraper_static_fn_base_with_L<Tr> base_type;
                typedef std::function<Tr(lua_State *, TParam...)> value_type;


                // 动态参数个数
                static int LuaCFunction(lua_State *L) {
                    value_type *fn = reinterpret_cast<value_type *>(lua_touserdata(L, lua_upvalueindex(1)));
                    if (NULL == fn) {
                        // 找不到函数
                        return 0;
                    }

                    return base_type::template run_fn<
                        value_type, std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, *fn, typename build_args_index<TParam...>::index_seq_type());
                }
            };

            template <typename Tr, typename... TParam>
            struct unwraper_functor_fn : public unwraper_static_fn_base<Tr> {
                typedef unwraper_static_fn_base<Tr> base_type;
                typedef std::function<Tr(TParam...)> value_type;


                // 动态参数个数
                static int LuaCFunction(lua_State *L) {
                    value_type *fn = reinterpret_cast<value_type *>(lua_touserdata(L, lua_upvalueindex(1)));
                    if (NULL == fn) {
                        // 找不到函数
                        return 0;
                    }

                    return base_type::template run_fn<
                        value_type, std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, *fn, typename build_args_index<TParam...>::index_seq_type());
                }
            };


            /*************************************\
            |* 成员函数Lua绑定，动态参数个数          *|
            \*************************************/
            template <typename Tr, typename TClass, typename... TParam>
            struct unwraper_member_fn;

            template <typename Tr, typename TClass, typename... TParam>
            struct unwraper_member_fn<Tr, TClass, lua_State *, TParam...> : public unwraper_static_fn_base_with_L<Tr> {
                typedef unwraper_static_fn_base_with_L<Tr> base_type;

                // 动态参数个数 - 成员函数
                static int LuaCFunction(lua_State *L, TClass *obj, Tr (TClass::*fn)(lua_State *, TParam...)) {
                    auto fn_wraper = [obj, fn](lua_State *L, TParam &&... args) { return (obj->*fn)(L, std::forward<TParam>(args)...); };

                    return base_type::template run_fn<
                        decltype(fn_wraper), std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, fn_wraper, typename build_args_index<TParam...>::index_seq_type());
                }

                // 动态参数个数 - 常量成员函数
                static int LuaCFunction(lua_State *L, TClass *obj, Tr (TClass::*fn)(lua_State *, TParam...) const) {
                    auto fn_wraper = [obj, fn](lua_State *L, TParam &&... args) { return (obj->*fn)(L, std::forward<TParam>(args)...); };

                    return base_type::template run_fn<
                        decltype(fn_wraper), std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, fn_wraper, typename build_args_index<TParam...>::index_seq_type());
                }
            };

            template <typename Tr, typename TClass, typename... TParam>
            struct unwraper_member_fn : public unwraper_static_fn_base<Tr> {
                typedef unwraper_static_fn_base<Tr> base_type;

                // 动态参数个数 - 成员函数
                static int LuaCFunction(lua_State *L, TClass *obj, Tr (TClass::*fn)(TParam...)) {
                    auto fn_wraper = [obj, fn](TParam &&... args) { return (obj->*fn)(std::forward<TParam>(args)...); };

                    return base_type::template run_fn<
                        decltype(fn_wraper), std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, fn_wraper, typename build_args_index<TParam...>::index_seq_type());
                }

                // 动态参数个数 - 常量成员函数
                static int LuaCFunction(lua_State *L, TClass *obj, Tr (TClass::*fn)(TParam...) const) {
                    auto fn_wraper = [obj, fn](TParam &&... args) { return (obj->*fn)(std::forward<TParam>(args)...); };

                    return base_type::template run_fn<
                        decltype(fn_wraper), std::tuple<typename std::remove_cv<typename std::remove_reference<TParam>::type>::type...> >(
                        L, fn_wraper, typename build_args_index<TParam...>::index_seq_type());
                }
            };
        }  // namespace detail
    }      // namespace lua
}  // namespace script

#endif
