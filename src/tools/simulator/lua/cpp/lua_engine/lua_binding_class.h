#ifndef SCRIPT_LUA_LUABINDINGCLASS
#define SCRIPT_LUA_LUABINDINGCLASS

#pragma once

#include <assert.h>
#include <cstdio>
#include <functional>
#include <list>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>

#include <config/atframe_utils_build_feature.h>
#include <design_pattern/noncopyable.h>
#include <std/explicit_declare.h>

#include "lua_binding_mgr.h"
#include "lua_binding_namespace.h"
#include "lua_binding_unwrapper.h"
#include "lua_binding_utils.h"
#include "lua_binding_wrapper.h"

namespace script {
namespace lua {

template <typename TClass, typename TProxy, bool>
struct convert_proxy_to_class;

template <typename TClass, typename TProxy>
struct convert_proxy_to_class<TClass, TProxy, true> {
  static TClass *convert(TProxy &proxy_object) {
#if defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI
    return dynamic_cast<TClass *>(&proxy_object);
#else
    return static_cast<TClass *>(&proxy_object);
#endif
  }
};

template <typename TClass, typename TProxy>
struct convert_proxy_to_class<TClass, TProxy, false> {
  static TClass *convert(TProxy &proxy_object) {
#if defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI
    return dynamic_cast<TClass *>(proxy_object());
#else
    return static_cast<TClass *>(proxy_object());
#endif
  }
};

/**
 * lua 类，注意只能用于局部变量
 *
 * @author  owent
 * @date    2014/10/25
 */
template <typename TClass, typename TProxy = TClass>
class lua_binding_class : public atfw::util::design_pattern::noncopyable {
 public:
  using value_type = TClass;
  using proxy_type = TProxy;
  using self_type = lua_binding_class<value_type, proxy_type>;
  using static_method = lua_binding_namespace::static_method;
  using userdata_type = typename lua_binding_userdata_info<proxy_type>::userdata_type;
  using pointer_type = typename lua_binding_userdata_info<proxy_type>::pointer_type;
  using userdata_ptr_type = typename lua_binding_userdata_info<proxy_type>::userdata_ptr_type;
  using member_proxy_method_t = std::function<int(lua_State *, value_type *)>;

  enum FUNC_TYPE {
    FT_NEW = 0,
    FT_GC,
    FT_TOSTRING,
    FT_MAX,
  };

 private:
  lua_CFunction default_funcs_[FT_MAX];

 public:
  lua_binding_class(const char *lua_name, const char *namespace_, lua_State *L)
      : lua_state_(L),
        lua_class_name_(lua_name),
        owner_ns_(namespace_, L),
        class_static_table_(0),
        class_member_table_(0),
        class_metatable_(0) {
    register_class();

    for (int i = 0; i < FT_MAX; ++i) {
      default_funcs_[i] = nullptr;
    }
  }

  lua_binding_class(const char *lua_name, lua_binding_namespace &ns)
      : lua_state_(ns.get_lua_state()),
        lua_class_name_(lua_name),
        owner_ns_(ns),
        class_static_table_(0),
        class_member_table_(0),
        class_metatable_(0) {
    register_class();
    memset(default_funcs_, nullptr, sizeof(default_funcs_));
  }

  ~lua_binding_class() { finish_class(); }

  /**
   * Gets the owner namespace.
   *
   * @return  The owner namespace.
   */

  lua_binding_namespace &get_owner_namespace() { return owner_ns_; }

  const lua_binding_namespace &get_owner_namespace() const { return owner_ns_; }

  /**
   * 获取静态class的table
   *
   * @return  The static class table.
   */
  int get_static_class_table() const { return class_static_table_; }

  /**
   * 获取类成员的table
   *
   * @return  The static class table.
   */
  int get_member_table() const { return class_member_table_; }

  /**
   * 获取UserData的映射关系MetaTable
   *
   * @return  The static class table.
   */
  int get_user_meta_table() const { return class_metatable_; }

  lua_State *get_lua_state() { return lua_state_; }

  /**
   * 添加常量(自动类型推断)
   *
   * @return  self.
   */
  template <typename Ty>
  self_type &add_const(const char *const_name, Ty n) {
    lua_State *state = get_lua_state();
    int ret_num = detail::wraper_var<Ty>::wraper(state, n);
    if (ret_num > 0) lua_setfield(state, get_static_class_table(), const_name);

    return *this;
  }

  /**
   * 添加常量(字符串)
   *
   * @return  self.
   */
  self_type &add_const(const char *const_name, const char *n, size_t s) {
    lua_State *state = get_lua_state();
    lua_pushstring(state, const_name);
    lua_pushlstring(state, n, s);
    lua_settable(state, get_static_class_table());

    return *this;
  }

  /**
   * 给类添加方法，自动推断类型
   *
   * @tparam  TF  Type of the tf.
   * @param   func_name   Name of the function.
   * @param   fn          The function.
   */
  template <typename R, typename... TParam>
  self_type &add_method(const char *func_name, R (*fn)(TParam... param)) {
    lua_State *state = get_lua_state();
    lua_pushstring(state, func_name);
    lua_pushlightuserdata(state, reinterpret_cast<void *>(fn));
    lua_pushcclosure(state, detail::unwraper_static_fn<R, TParam...>::LuaCFunction, 1);
    lua_settable(state, get_static_class_table());

    return (*this);
  }

  /**
   * 给类添加仿函数，自动推断类型
   *
   * @tparam  R           Type of the r.
   * @tparam  TParam      Type of the parameter.
   * @param   func_name   Name of the function.
   * @param   fn          functor
   *
   * @return  A self_type&amp;
   */
  template <typename R, typename... TParam>
  self_type &add_method(const char *func_name, std::function<R(TParam...)> fn) {
    using fn_t = std::function<R(TParam...)>;

    lua_State *state = get_lua_state();
    lua_pushstring(state, func_name);

    lua_binding_placement_new_and_delete<fn_t>::create(state, fn);
    lua_pushcclosure(state, detail::unwraper_functor_fn<R, TParam...>::LuaCFunction, 1);
    lua_settable(state, get_static_class_table());

    return (*this);
  }

  /**
   * 添加成员方法
   *
   * @tparam  R       Type of the r.
   * @tparam  TParam  Type of the parameter.
   * @param   func_name                   Name of the function.
   * @param [in,out]  fn)(TParam...param) If non-null, the fn)( t param...param)
   *
   * @return  A self_type&amp;
   */
  template <typename R, typename TClassIn, typename... TParam>
  self_type &add_method(const char *func_name, R (TClassIn::*fn)(TParam... param)) {
    static_assert(std::is_convertible<value_type *, TClassIn *>::value, "class of member method invalid");

    lua_State *state = get_lua_state();
    lua_pushstring(state, func_name);

    member_proxy_method_t *fn_ptr = lua_binding_placement_new_and_delete<member_proxy_method_t>::create(state);
    *fn_ptr = [fn](lua_State *L, value_type *pobj) {
#if defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI
      return detail::unwraper_member_fn<R, TClassIn, TParam...>::LuaCFunction(L, dynamic_cast<TClassIn *>(pobj), fn);
#else
      return detail::unwraper_member_fn<R, TClassIn, TParam...>::LuaCFunction(L, static_cast<TClassIn *>(pobj), fn);
#endif
    };
    lua_pushcclosure(state, __member_method_unwrapper, 1);
    lua_settable(state, get_member_table());

    return (*this);
  }

  /**
   * 添加常量成员方法
   *
   * @tparam  R       Type of the r.
   * @tparam  TParam  Type of the parameter.
   * @param   func_name       Name of the function.
   * @param [in,out]  const   If non-null, the constant.
   *
   * @return  A self_type&amp;
   */
  template <typename R, typename TClassIn, typename... TParam>
  self_type &add_method(const char *func_name, R (TClassIn::*fn)(TParam... param) const) {
    static_assert(std::is_convertible<value_type *, TClassIn *>::value, "class of member method invalid");

    lua_State *state = get_lua_state();
    lua_pushstring(state, func_name);

    member_proxy_method_t *fn_ptr = lua_binding_placement_new_and_delete<member_proxy_method_t>::create(state);
    *fn_ptr = [fn](lua_State *L, const value_type *pobj) {
#if defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI
      return detail::unwraper_member_fn<R, TClassIn, TParam...>::LuaCFunction(L, dynamic_cast<const TClassIn *>(pobj),
                                                                              fn);
#else
      return detail::unwraper_member_fn<R, TClassIn, TParam...>::LuaCFunction(L, static_cast<const TClassIn *>(pobj),
                                                                              fn);
#endif
    };
    lua_pushcclosure(state, __member_method_unwrapper, 1);
    lua_settable(state, get_member_table());

    return (*this);
  }

  template <typename R, typename TClassIn, typename... TParam>
  self_type &add_method_select_const(const char *func_name, R (TClassIn::*fn)(TParam... param) const) {
    return add_method(func_name, fn);
  }

  /**
   * 转换为namespace，注意有效作用域是返回的lua_binding_namespace和这个Class的子集
   *
   * @return  The static class table.
   */
  lua_binding_namespace &as_namespace() {
    // 第一次获取时初始化
    if (as_ns_.ns_.empty()) {
      as_ns_.ns_ = owner_ns_.ns_;
      as_ns_.ns_.push_back(get_lua_name());
      as_ns_.base_stack_top_ = 0;
      as_ns_.this_ns_ = class_static_table_;
    }

    return as_ns_;
  }

  template <class TParent>
  void inherit(lua_State *L) {
    static_assert(!std::is_same<TClass, TParent>::value, "child and parent type can not be the same");
    static_assert(std::is_base_of<TParent, TClass>::value, "current type not inherit from specify parent type");

    lua_auto_block guard(L);

    lua_getfield(L, class_metatable_, "__inherits");  // Stack +1 : (matetable["__inherits"])
    if (lua_isnoneornil(L, -1)) {
      // Create one if it's not exists
      lua_createtable(L, 1, 0);  // Stack +2 : (nil, NEW TABLE)
#if LUA_VERSION_NUM > 501
      lua_copy(L, -1, -2);  // Stack +2 : (NEW TABLE, NEW TABLE)
#else
      lua_pushvalue(L, -1);               // Stack +3 : (nil, NEW TABLE, NEW TABLE)
      lua_replace(L, lua_gettop(L) - 2);  // Stack +2 : (NEW TABLE, NEW TABLE)
#endif
      lua_setfield(L, class_metatable_, "__inherits");  // Stack +1 : (matetable["__inherits"])
    }

    // append parent metatable name into __inherits

#if LUA_VERSION_NUM >= 502
    auto len = lua_rawlen(L, -1);
#else
    lua_rawlen(L, -1);  // Stack +2 : (matetable["__inherits"], #matetable["__inherits"])
    auto len = lua_tointeger(L, -1);
    lua_pop(L, 1);  // Stack +1 : (matetable["__inherits"])
#endif

    const char *parent_metatable_name = lua_binding_metatable_info<TParent>::get_lua_metatable_name();
    lua_pushstring(L, parent_metatable_name);  // Stack +2 : (matetable["__inherits"], parent_metatable_name)
#if LUA_VERSION_NUM >= 503
    lua_rawseti(L, -2, static_cast<lua_Integer>(len + 1));  // Stack +1: (matetable["__inherits"])
#else
    lua_rawseti(L, -2, static_cast<int>(len + 1));  // Stack +1: (matetable["__inherits"])
#endif
    // RAII reset top
  }

  const char *get_lua_name() const { return lua_class_name_.c_str(); }

  static const char *get_lua_metatable_name() {
    return lua_binding_metatable_info<value_type>::get_lua_metatable_name();
  }

  self_type &set_new(lua_CFunction f, const std::string &method_name = "new") noexcept {
    lua_State *state = get_lua_state();
    // new 方法
    lua_pushstring(state, method_name.c_str());
    lua_pushcfunction(state, f);
    lua_settable(state, class_static_table_);

    default_funcs_[FT_NEW] = f;

    return (*this);
  }

  template <typename... TParams>
  self_type &set_default_new(const std::string &method_name = "new") {
    using new_fn_t = std::function<pointer_type(TParams && ...)>;
    lua_State *L = get_lua_state();

    new_fn_t fn = [L](TParams &&...params) { return create<TParams &&...>(L, params...); };

    return add_method<pointer_type, TParams &&...>(method_name.c_str(), fn);
  }

  self_type &set_to_string(lua_CFunction f) noexcept {
    lua_State *state = get_lua_state();
    // __tostring方法
    lua_pushliteral(state, "__tostring");
    lua_pushcfunction(state, f);
    lua_settable(state, class_metatable_);

    default_funcs_[FT_TOSTRING] = f;

    return (*this);
  }

  self_type &set_gc(lua_CFunction f) noexcept {
    lua_State *state = get_lua_state();
    // 垃圾回收方法（注意函数内要判断排除table类型）
    lua_pushliteral(state, "__gc");
    lua_pushcfunction(state, f);
    lua_settable(state, class_metatable_);

    default_funcs_[FT_GC] = f;

    return (*this);
  }

 private:
  /**
   * Registers the class.
   *
   * @author
   * @date    2014/10/25
   */
  void register_class() {
    // 初始化后就不再允许新的类注册了
    lua_binding_class_mgr_inst<proxy_type>::me();

    lua_State *state = get_lua_state();
    // 注册C++类
    {
      lua_newtable(state);
      class_static_table_ = lua_gettop(state);

      lua_newtable(state);
      class_member_table_ = lua_gettop(state);

      luaL_newmetatable(state, get_lua_metatable_name());
      class_metatable_ = lua_gettop(state);

      // 注册类到namespace
      // 注意__index留空
      lua_pushstring(state, get_lua_name());
      lua_pushvalue(state, class_static_table_);
      lua_settable(state, owner_ns_.get_namespace_table());

      // table 的__type默认设为native class(这里仅仅是为了和class.lua交互，如果不设置的话默认就是native code)
      lua_pushliteral(state, "__name");
      lua_pushfstring(state, "[native class: %s]", get_lua_name());
      lua_settable(state, class_static_table_);

      // memtable 的__type默认设为native object(这里仅仅是为了和class.lua交互，如果不设置的话默认就是native code)
      lua_pushliteral(state, "__name");
      lua_pushfstring(state, "[native class: %s]", get_lua_name());
      lua_settable(state, class_member_table_);

      /**
       * metatable 初始化(userdata映射表)
       */

      // 继承关系链 userdata -> metatable(实例接口表): memtable(成员接口表): table(静态接口表) : ...
      lua_pushliteral(state, "__index");
      lua_pushcfunction(state, __vtables_method_lookup_index);
      lua_settable(state, class_metatable_);

      lua_pushliteral(state, "__name");
      lua_pushfstring(state, "[native class: %s]", get_lua_name());
      lua_settable(state, class_metatable_);

      // 新增符号查找表, -> { __vtables = { member_table, static_table, ... } }
      lua_pushliteral(state, "__vtables");
      lua_createtable(state, 4, 0);
      int vtable_index = lua_gettop(state);
      lua_pushinteger(state, 1);
      lua_pushvalue(state, class_member_table_);
      lua_settable(state, vtable_index);

      lua_pushinteger(state, 2);
      lua_pushvalue(state, class_static_table_);
      lua_settable(state, vtable_index);

      // Set class_metatable_["__vtables"] = vtable_index
      lua_settable(state, class_metatable_);
    }
  }

  void finish_class() noexcept {
    if (nullptr == default_funcs_[FT_TOSTRING]) set_to_string(__tostring);

    if (nullptr == default_funcs_[FT_GC]) set_gc(__lua_gc);
  }

  //================ 以下方法皆为lua接口，并提供给C++层使用 ================
 public:
  /**
   * 创建新实例，并把相关的lua对象入栈
   *
   * @param [in,out]  L   If non-null, the lua_State * to process.
   *
   * @return  null if it fails, else a pointer_type.
   */
  template <typename... TParams>
  static pointer_type create(lua_State *L, TParams &&...params) {
    pointer_type obj = std::make_shared<proxy_type>(std::forward<TParams>(params)...);

    // 添加到缓存表，防止被立即析构
    lua_binding_class_mgr_inst<proxy_type>::me()->add_ref(L, obj);
    return obj;
  }

 private:
  /**
   * __tostring 方法
   *
   * @author
   * @date    2014/10/25
   *
   * @param [in,out]  L   If non-null, the lua_State * to process.
   *
   * @return  An int.
   */

  static int __tostring(lua_State *L) {
    if (lua_gettop(L) <= 0) {
      lua_pushliteral(L, "[native code]");
      return lua_error(L);
    }

    if (!lua_isuserdata(L, 1)) {
      if (lua_isstring(L, 1)) {
        lua_pushvalue(L, 1);
        return 1;
      }

      if (lua_isboolean(L, 1)) {
        lua_pushstring(L, (lua_toboolean(L, 1) ? "true" : "false"));
        return 1;
      }

      if (lua_isnil(L, 1)) {
        lua_pushliteral(L, "nil");
        return 1;
      }

#if LUA_VERSION_NUM >= 503
      if (lua_isinteger(L, 1)) {
        lua_pushfstring(L, "%I", lua_tointeger(L, 1));
        return 1;
      }
#endif
      const char *kind = lua_typename(L, lua_type(L, 1));
      lua_pushfstring(L, "%s: %p", kind, lua_topointer(L, 1));
      return 1;
    }

    std::stringstream ss;
    pointer_type real_ptr(nullptr);

    userdata_ptr_type pobj = static_cast<userdata_ptr_type>(lua_touserdata(L, 1));
    if (pobj) {
      real_ptr = pobj->lock();
    }

    lua_pushliteral(L, "__name");
    lua_gettable(L, 1);

    if (lua_isstring(L, -1)) {
      ss << lua_tostring(L, -1);
    } else {
      ss << "[native code]";
    }
    lua_pop(L, 1);
    if (real_ptr) {
      ss << " @ "
         << convert_proxy_to_class<value_type, proxy_type,
                                   std::is_convertible<proxy_type *, value_type *>::value>::convert(*real_ptr);
    } else {
      ss << " @ nil";
    }

    std::string str = ss.str();
    lua_pushlstring(L, str.c_str(), str.size());
    return 1;
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
      fn::print_traceback(L, "userdata __gc is called without self");
      return 0;
    }

    // metatable表触发
    if (0 == lua_isuserdata(L, 1)) {
      lua_remove(L, 1);
      return 0;
    }

    // 析构
    userdata_ptr_type pobj = static_cast<userdata_ptr_type>(lua_touserdata(L, 1));
    pobj->~userdata_type();

    return 0;
  }

  /**
   * __call 方法，不存在的方法要输出错误
   */
  // static int __call(lua_State *L) {
  //	WLOGERROR("lua try to call invalid member method [%s].%s(%d parameters)\n",
  //        get_lua_metatable_name(),
  //        luaL_checklstring(L, 1, nullptr),
  //        lua_gettop(L) - 1
  //    );
  //    return 0;
  //}

  static int __member_method_unwrapper(lua_State *L) {
    member_proxy_method_t *fn = reinterpret_cast<member_proxy_method_t *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (nullptr == fn) {
      return luaL_error(L, "lua try to call member method in class %s but fn not set.", get_lua_metatable_name());
    }

    if (lua_gettop(L) < 1) {
      return luaL_error(L, "can not call member method in class %s with self=nil.", get_lua_metatable_name());
    }

    if (!lua_isuserdata(L, 1)) {
      return luaL_error(L, "can not call member method with self type %s.", lua_typename(L, lua_type(L, 1)));
    }

    const char *class_name = get_lua_metatable_name();
    do {
      // 验证类型和继承关系
      luaL_getmetatable(L, class_name);  // Stack +1: (class metatable)
      lua_getmetatable(L, 1);            // Stack +2: (class metatable, userdata metatable)

      if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return luaL_error(L, "can not find metatable for self");
      }

      if (!lua_istable(L, -2)) {
        lua_pop(L, 2);
        return luaL_error(L, "can not find metatable \"%s\"", class_name);
      }

      // 类型相等
      if (lua_rawequal(L, -1, -2)) {
        lua_pop(L, 2);  // Stack +0
        break;
      }

      // 检测继承
      lua_getfield(L, -1,
                   "__inherits");  // Stack +3: (class metatable, userdata metatable, userdata metatable["__inherits"])
      if (lua_istable(L, -1)) {
        bool check_passed = false;
        for (lua_Integer i = 1;; ++i) {
          lua_rawgeti(L, -1,
                      i);  // Stack +4: (class metatable, userdata metatable, userdata metatable["__inherits"], userdata
                           // metatable["__inherits"][i])
          if (lua_rawequal(L, -1, -4)) {
            lua_pop(L, 4);  // Stack +0
            check_passed = true;
            break;
          }

          if (lua_isnoneornil(L, -1)) {
            lua_pop(L, 1);  // Stack +3: (class metatable, userdata metatable, userdata metatable["__inherits"])
            break;
          }
          lua_pop(L, 1);  // Stack +3: (class metatable, userdata metatable, userdata metatable["__inherits"])
        }

        if (check_passed) {
          break;
        }
      }
      lua_pop(L, 1);                  // Stack +2: (class metatable, userdata metatable)
      lua_getfield(L, -1, "__name");  // Stack +3: (class metatable, userdata metatable, name1)
      const char *userdata_type_name = nullptr;
      if (lua_isstring(L, -1)) {
        userdata_type_name = lua_tostring(L, -1);
      }

      lua_getfield(L, -3, "__name");  // Stack +4: (class metatable, userdata metatable, name1, name2)
      const char *class_type_name = nullptr;
      if (lua_isstring(L, -1)) {
        class_type_name = lua_tostring(L, -1);
      }
      if (nullptr == class_type_name) {
        lua_pushfstring(L, "%s is not base of userdata %s", class_name, userdata_type_name ? userdata_type_name : "");
      } else {
        lua_pushfstring(L, "%s(%s) is not base of userdata %s", class_name, class_type_name,
                        userdata_type_name ? userdata_type_name : "");
      }
      // Stack +5: (class metatable, userdata metatable, name1, name2, error message)
      lua_replace(L, lua_gettop(L) - 4);  // Stack +4: (error message, userdata metatable, name1, name2)
      lua_pop(L, 3);                      // Stack +1: (error message)
      return lua_error(L);
    } while (false);
    userdata_ptr_type pobj = static_cast<userdata_ptr_type>(lua_touserdata(L, 1));

    if (nullptr == pobj) {
      FWLOGERROR("lua try to call {}'s member method but self not set or type error.", class_name);
      fn::print_traceback(L, "");
      return 0;
    }

    pointer_type proxy_ptr = pobj->lock();
    lua_remove(L, 1);

    if (!proxy_ptr) {
      FWLOGERROR("lua try to call {}'s member method but this=nullptr(proxy destroyed).", class_name);
      fn::print_traceback(L, "");
      return 0;
    }

    // pointer_type -> value_type
    value_type *object_ptr =
        convert_proxy_to_class<value_type, proxy_type, std::is_convertible<proxy_type *, value_type *>::value>::convert(
            *proxy_ptr);
    if (!object_ptr) {
      FWLOGERROR("lua try to call {}'s member method but this=nullptr(convert failed).", class_name);
      fn::print_traceback(L, "");
      return 0;
    }

    return (*fn)(L, object_ptr);
  }

  static int __vtables_method_lookup_index(lua_State *L) { return __vtables_method_lookup_symbol(L, -1, 1); }

  static int __vtables_method_lookup_symbol(lua_State *L, int key_index, int start_vtable_field_index) {
    const char *class_name = get_lua_metatable_name();

    if (lua_isnoneornil(L, key_index)) {
      FWLOGERROR("lua try to lookup {}'s symbol but key is nil or none.", class_name);
      fn::print_traceback(L, "");
      return 1;
    }

    if (key_index < 0) {
      key_index = lua_gettop(L) + 1 + key_index;
    }

    luaL_getmetatable(L, class_name);  // Stack: +1(metatable)
    int metatable_index = lua_gettop(L);
    if (lua_isnoneornil(L, -1)) {
      FWLOGERROR("lua try to lookup {}'s symbol but metatable not found.", class_name);
      fn::print_traceback(L, "");
      // Stack: +1(nil)
      return 1;
    }

    lua_getfield(L, metatable_index, "__vtables");  // Stack: +2(metatable, __vtables)
    if (lua_isnoneornil(L, -1)) {
      FWLOGERROR("lua try to lookup {}'s symbol but metatable.__vtables not found.", class_name);
      fn::print_traceback(L, "");

      lua_remove(L, lua_gettop(L) - 1);  // Stack: +1(nil)
      return 1;
    }

    int vtable_index = lua_gettop(L);
    int vtable_field_index = start_vtable_field_index;
    for (;; ++vtable_field_index) {
      lua_rawgeti(L, vtable_index,
                  vtable_field_index);  // Stack: +3(metatable, __vtables, __vtables[vtable_field_index])
      if (lua_isnoneornil(L, -1)) {
        lua_pop(L, 1);  // Stack: +2(metatable, __vtables)
        break;
      }

      lua_pushvalue(L, key_index);  // Stack: +4(metatable, __vtables, __vtables[vtable_field_index], KEY)
      lua_gettable(
          L, -2);  // Stack: +4(metatable, __vtables, __vtables[vtable_field_index], __vtables[vtable_field_index][KEY])
      if (!lua_isnoneornil(L, -1)) {
        lua_replace(L,
                    lua_gettop(L) -
                        3);  // Stack: +3(__vtables[vtable_field_index][KEY], __vtables, __vtables[vtable_field_index])
        lua_pop(L, 2);       // Stack: +1(__vtables[vtable_field_index][KEY])
        return 1;
      }

      lua_pop(L, 2);  // Stack: +2(metatable, __vtables)
    }

    // Stack: +2(metatable, __vtables)
    // Complete inherit vtables
    lua_getfield(L, metatable_index, "__inherits");  // Stack: +3(metatable, __vtables, metatable["__inherits"])
    if (lua_isnoneornil(L, -1)) {
      lua_replace(L, lua_gettop(L) - 2);  // Stack: +2(nil, __vtables)
      lua_pop(L, 1);                      // Stack: +1(nil)
      return 1;
    }

    int inherit_table_index = lua_gettop(L);

#if LUA_VERSION_NUM >= 502
    lua_Integer inherit_table_len = static_cast<lua_Integer>(lua_rawlen(L, inherit_table_index));
#else
    lua_rawlen(L, -1);  // Stack: +4(metatable, __vtables, metatable["__inherits"], #metatable["__inherits"])
    lua_Integer inherit_table_len = static_cast<lua_Integer>(lua_tointeger(L, inherit_table_index));
    lua_pop(L, 1);  // Stack: +3(metatable, __vtables, metatable["__inherits"])
#endif
    int vtable_next_field_index = vtable_field_index;
    // Stack: +3(metatable, __vtables, metatable["__inherits"])
    for (auto i = 1; i <= inherit_table_len; ++i) {
      lua_rawgeti(L, inherit_table_index,
                  i);  // Stack: +4(metatable, __vtables, metatable["__inherits"], metatable["__inherits"][i])
      if (!lua_isstring(L, -1)) {
        lua_pop(L, i);  // Stack: +3(metatable, __vtables, metatable["__inherits"])
        continue;
      }

      const char *inherit_class_name = lua_tostring(L, -1);
      luaL_getmetatable(L, inherit_class_name);  // Stack: +5(metatable, __vtables, metatable["__inherits"],
                                                 // metatable["__inherits"][i], parent_metatable)
      if (lua_istable(L, -1)) {
        lua_createtable(L, 0, 0);  // Stack: +6(metatable, __vtables, metatable["__inherits"],
                                   // metatable["__inherits"][i], parent_metatable, {})
        lua_pushvalue(L,
                      -2);  // Stack: +7(metatable, __vtables, metatable["__inherits"], metatable["__inherits"][i],
                            // parent_metatable, {}, parent_metatable)
        lua_setmetatable(L, -2);  // Stack: +6(metatable, __vtables, metatable["__inherits"],
                                  // metatable["__inherits"][i], parent_metatable, {})

        // Append __vtables[vtable_next_field_index] = {}(with metatable = parent_metatable)
        lua_rawseti(L, vtable_index,
                    vtable_next_field_index);  // Stack: +5(metatable, __vtables, metatable["__inherits"],
                                               // metatable["__inherits"][i], parent_metatable)
        ++vtable_next_field_index;

        // Set metatable["__inherits"][i] = parent_metatable
        lua_rawseti(L, inherit_table_index,
                    i);  // Stack: +4(metatable, __vtables, metatable["__inherits"], metatable["__inherits"][i])
        lua_pop(L, 1);   // Stack: +3(metatable, __vtables, metatable["__inherits"])
      } else {
        FWLOGERROR("lua try to lookup {}'s inherit class {} but metatable not found.", class_name, inherit_class_name);
        fn::print_traceback(L, "");
        lua_createtable(
            L, 0, 0);  // Stack: +6(metatable, __vtables, metatable["__inherits"], metatable["__inherits"][i], nil, {})
        // Set metatable["__inherits"][i] = {}
        lua_rawseti(L, inherit_table_index,
                    i);  // Stack: +5(metatable, __vtables, metatable["__inherits"], metatable["__inherits"][i], nil)
        lua_pop(L, 2);   // Stack: +3(metatable, __vtables, metatable["__inherits"])
      }
    }

    // Reset stack
    lua_settop(L, metatable_index - 1);
    if (vtable_next_field_index > vtable_field_index) {
      return __vtables_method_lookup_symbol(L, key_index, vtable_field_index);
    }

    lua_pushnil(L);
    return 1;
  }

 private:
  lua_State *lua_state_;

  std::string lua_class_name_;
  lua_binding_namespace owner_ns_;
  lua_binding_namespace as_ns_;

  int class_static_table_; /**< 公共类型的Lua Table*/
  int class_member_table_; /**< The class table*/
  int class_metatable_;    /**< The class table*/
};
}  // namespace lua
}  // namespace script
#endif
