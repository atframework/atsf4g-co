#ifndef SCRIPT_LUA_LUABINDINGUTILS
#define SCRIPT_LUA_LUABINDINGUTILS

#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <typeinfo>

#include <config/atframe_utils_build_feature.h>
#include <log/log_wrapper.h>

#include <config/compiler_features.h>
#include <std/explicit_declare.h>

#include "../lua_module/lua_adaptor.h"

namespace script {
namespace lua {

class lua_auto_block {
 public:
  lua_auto_block(lua_State *);

  ~lua_auto_block();

  void null_call();

 private:
  lua_auto_block(const lua_auto_block &src) = delete;
  const lua_auto_block &operator=(const lua_auto_block &src) = delete;

  lua_State *state_;
  int stack_top_;
};

#if !(defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI)
std::string lua_binding_userdata_generate_metatable_name();
#endif

template <typename TClass>
struct lua_binding_metatable_info {
  using value_type = TClass;
  static const char *get_lua_metatable_name() {
#if defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI
    return typeid(value_type).name();
#else
    if (!metatable_name_.empty()) {
      return metatable_name_.c_str();
    }

    metatable_name_ = lua_binding_userdata_generate_metatable_name();
    return metatable_name_.c_str();
#endif
  }

#if !(defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI)
  static std::string metatable_name_;
#endif
};

#if !(defined(ATFRAMEWORK_UTILS_ENABLE_RTTI) && ATFRAMEWORK_UTILS_ENABLE_RTTI)
template <typename TClass>
std::string lua_binding_metatable_info<TClass>::metatable_name_;
#endif

template <typename TProxy>
struct lua_binding_userdata_info {
  using value_type = TProxy;
  using pointer_type = std::shared_ptr<value_type>;
  using userdata_type = std::weak_ptr<value_type>;
  using userdata_ptr_type = userdata_type *;
};

/**
 * 特殊实例（整个对象直接存在userdata里）
 *
 * @tparam  TC  Type of the tc.
 */

template <typename TC>
struct lua_binding_placement_new_and_delete {
  using value_type = TC;

  static void push_metatable(lua_State *L) {
    const char *class_name = lua_binding_metatable_info<value_type>::get_lua_metatable_name();
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
  static value_type *create(lua_State *L, TParams &&...params) {
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
      FWLOGERROR("userdata __gc is called without self");
      return 0;
    }

    // metatable表触发
    if (0 == lua_isuserdata(L, 1)) {
      lua_remove(L, 1);
      return 0;
    }

    auto obj = static_cast<value_type *>(
        luaL_checkudata(L, 1, lua_binding_metatable_info<value_type>::get_lua_metatable_name()));
    if (nullptr == obj) {
      FWLOGERROR("try to convert userdata to {} but failed",
                 lua_binding_metatable_info<value_type>::get_lua_metatable_name());
      return 0;
    }
    // 执行析构，由lua负责释放内存
    obj->~value_type();
    return 0;
  }
};

namespace fn {
int get_pcall_hmsg(lua_State *L);

/**
 * @brief load _G.path and push it into stack
 * @param L lua State
 * @param path path
 * @param auto_create_table create table if not exists
 * @note this API always push a value into stack, push nil if failed
 * @return true if success
 */
bool load_item(lua_State *L, const std::string &path, bool auto_create_table = false);

/**
 * @brief load _G.path and push it into stack
 * @param L lua State
 * @param path path
 * @param auto_create_table create table if not exists
 * @note this API always push a value into stack, push nil if failed
 * @return true if success
 */
bool load_item(lua_State *L, const char *path, bool auto_create_table = false);

/**
 * @brief load path from table and push it into stack
 * @param L lua State
 * @param path path
 * @param table_index stack index of table to load
 * @param auto_create_table create table if not exists
 * @note this API always push a value into stack, push nil if failed
 * @return true if success
 */
bool load_item(lua_State *L, const std::string &path, int table_index, bool auto_create_table = false);

/**
 * @brief load path from table and push it into stack
 * @param L lua State
 * @param path path
 * @param table_index stack index of table to load
 * @param auto_create_table create table if not exists
 * @note this API always push a value into stack, push nil if failed
 * @return true if success
 */
bool load_item(lua_State *L, const char *path, int table_index, bool auto_create_table = false);

bool remove_item(lua_State *L, const std::string &path);

bool remove_item(lua_State *L, const char *path);

bool remove_item(lua_State *L, const std::string &path, int table_index);

bool remove_item(lua_State *L, const char *path, int table_index);

void print_stack(lua_State *L);

void print_traceback(lua_State *L, const std::string &msg);

/**
 * @brief run file
 * @param L lua State
 * @param file_path file path
 * @note this call will not push anything into stack
 * @return true if success
 */
bool exec_file(lua_State *L, const char *file_path);

/**
 * @brief run code
 * @param L lua State
 * @param codes lua code
 * @note this call will not push anything into stack
 * @return true if success
 */
bool exec_code(lua_State *L, const char *codes);

/**
 * @brief run file with custom envirment table
 * @param L lua State
 * @param file_path file path
 * @param envidx environment table index
 * @note this call will not push anything into stack
 * @return true if success
 */
bool exec_file_with_env(lua_State *L, const char *file_path, int envidx);

/**
 * @brief run code with custom envirment table
 * @param L lua State
 * @param codes lua code
 * @param envidx environment table index
 * @note this call will not push anything into stack
 * @return true if success
 */
bool exec_code_with_env(lua_State *L, const char *codes, int envidx);

/**
 * @brief run file with cached custom envirment table
 * @param L lua State
 * @param file_path file path
 * @param env_cache_path environment cache path
 * @note this call will not push anything into stack
 * @return true if success
 */
bool exec_file_with_protected_env(lua_State *L, const char *file_path, const char *env_cache_path);

/**
 * @brief run code with custom envirment table
 * @param L lua State
 * @param codes lua code
 * @param env_cache_path environment cache path
 * @note this call will not push anything into stack
 * @return true if success
 */
bool exec_code_with_protected_env(lua_State *L, const char *codes, const char *env_cache_path);

/**
 * @brief get environment at specify path or create a new environment table at specify path
 * @param L lua State
 * @param env_cache_path environment cache path
 * @return the number of values pushed into stack, return 0 if failed
 */
int mutable_env_table(lua_State *L, const char *env_cache_path);

int lua_stackdump(lua_State *L);

std::string lua_stackdump_to_string(lua_State *L);
}  // namespace fn
}  // namespace lua
}  // namespace script
#endif
