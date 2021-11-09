#include <cmath>
#include <cstdlib>
#include <ctime>
#include <list>
#include <sstream>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "lua_binding_utils.h"

namespace script {
namespace lua {
lua_auto_block::lua_auto_block(lua_State *L) : state_(L), stack_top_(lua_gettop(state_)) {}

lua_auto_block::~lua_auto_block() { lua_settop(state_, stack_top_); }

void lua_auto_block::null_call() {}

#if !(defined(LIBATFRAME_UTILS_ENABLE_RTTI) && LIBATFRAME_UTILS_ENABLE_RTTI)
std::string lua_binding_userdata_generate_metatable_name() {
  static util::lock::atomic_int_type<size_t> clazz_idx(0);
  std::stringstream ss;
  ss << "lua metatable " << (++clazz_idx);
  return ss.str();
}
#endif

namespace fn {
int get_pcall_hmsg(lua_State *L) {
  if (nullptr == L) return 0;

  lua_getglobal(L, "stackdump");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    lua_pushcfunction(L, lua_stackdump);
  }

  return lua_gettop(L);
}

bool load_item(lua_State *L, const std::string &path, bool auto_create_table) {
  return load_item(L, path.c_str(), auto_create_table);
}

bool load_item(lua_State *L, const std::string &path, int table_index, bool auto_create_table) {
  return load_item(L, path.c_str(), table_index, auto_create_table);
}

bool load_item(lua_State *L, const char *path, bool auto_create_table) {
#ifdef LUA_RIDX_GLOBALS
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  bool res = load_item(L, path, -1, auto_create_table);
  lua_remove(L, -2);
  return res;
#else
  return load_item(L, path, LUA_GLOBALSINDEX, auto_create_table);
#endif
}

bool load_item(lua_State *L, const char *path, int table_index, bool auto_create_table) {
  bool res = true;
  std::list<std::string> idents;
  // strtok(str, ".") is not thread safe
  const char *s = path, *e = path;
  while (e && *e) {
    if ('.' == *e) {
      idents.push_back(std::string(s, e));
      s = e + 1;
    }

    ++e;
  }
  if (s < e) idents.push_back(std::string(s, e));

  lua_pushvalue(L, table_index);
  for (std::list<std::string>::iterator iter = idents.begin(); res && iter != idents.end(); ++iter) {
    lua_getfield(L, -1, iter->c_str());
    if (lua_isnil(L, -1)) {
      if (auto_create_table) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, iter->c_str());
      } else {
        res = false;
      }
    }

    lua_remove(L, -2);
  }

  return res && !lua_isnil(L, -1);
}

bool remove_item(lua_State *L, const std::string &path) { return remove_item(L, path.c_str()); }

bool remove_item(lua_State *L, const std::string &path, int table_index) {
  return remove_item(L, path.c_str(), table_index);
}

bool remove_item(lua_State *L, const char *path) {
#ifdef LUA_RIDX_GLOBALS
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  bool res = remove_item(L, path, -1);
  lua_pop(L, 1);
  return res;
#else
  return remove_item(L, path, LUA_GLOBALSINDEX);
#endif
}

bool remove_item(lua_State *L, const char *path, int table_index) {
  lua_auto_block block(L);

  std::list<std::string> idents;
  // strtok(str, ".") is not thread safe
  const char *s = path, *e = path;
  while (e && *e) {
    if ('.' == *e) {
      idents.push_back(std::string(s, e));
      s = e + 1;
    }

    ++e;
  }
  if (s < e) idents.push_back(std::string(s, e));

  if (idents.empty()) return true;

  std::string last_ident = idents.back();
  idents.pop_back();

  lua_pushvalue(L, table_index);
  for (std::string &ident : idents) {
    lua_getfield(L, -1, ident.c_str());
    if (lua_isnil(L, -1)) {
      return true;
    }
  }

  lua_pushnil(L);
  lua_setfield(L, -2, last_ident.c_str());

  return true;
}

void print_stack(lua_State *L) {
  int iArgs = lua_gettop(L);
  // LUALOGCALL("STACK TOP:%d", iArgs);

  std::stringstream ss;
  for (int i = 1; i <= iArgs; i++) {
    ss << i << ":";

    if (lua_istable(L, i)) {
      ss << "table";
    } else if (lua_isnone(L, i)) {
      ss << "none";
    } else if (lua_isnil(L, i)) {
      ss << "nil";
    } else if (lua_isboolean(L, i)) {
      if (lua_toboolean(L, i) != 0)
        ss << "true";
      else
        ss << "false";
    } else if (lua_isfunction(L, i)) {
      ss << "function";
    } else if (lua_islightuserdata(L, i)) {
      ss << "lightuserdata";
    } else if (lua_isthread(L, i)) {
      ss << "thread";
    } else {
      const char *tinfo = lua_tostring(L, i);
      if (tinfo)
        ss << tinfo;
      else
        ss << lua_typename(L, lua_type(L, i));
    }

    if (i != iArgs) {
      ss << " |";
    }
  }

  FWLOGINFO("STACK :{}", ss.str());
}

void print_traceback(lua_State *L, const std::string &msg) {
  lua_auto_block autoBlock(L);
  FWLOGINFO("TRACEBACK:{}\n{}", msg, lua_stackdump_to_string(L));
}

bool exec_file(lua_State *L, const char *file_path) {
  lua_auto_block autoBlock(L);

  int hmsg = get_pcall_hmsg(L);
  if (luaL_loadfile(L, file_path) || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    return false;
  }

  return true;
}

bool exec_code(lua_State *L, const char *codes) {
  lua_auto_block autoBlock(L);
  int hmsg = get_pcall_hmsg(L);
  if (luaL_loadstring(L, codes) || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    return false;
  }

  return true;
}

bool exec_file_with_env(lua_State *L, const char *file_path, int envidx) {
  lua_auto_block autoBlock(L);

  int hmsg = get_pcall_hmsg(L);
  if (LUA_OK != luaL_loadfile(L, file_path)) {
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    return false;
  }

  if (envidx < 0) {
    envidx -= 2;
  }
  lua_pushvalue(L, envidx);
  // @see https://www.lua.org/manual/5.3/manual.html#2.2 and load_aux@lbaselib.c in lua source
  // @see https://www.lua.org/manual/5.1/manual.html#lua_setfenv
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM > 501
  if (nullptr == lua_setupvalue(L, -2, 1)) {
    lua_pop(L, 1);
    FWLOGERROR("{}", "lua_setupvalue failed");
    return false;
  }
#else
  if (0 == lua_setfenv(L, -2)) {
    FWLOGERROR("{}", "lua_setfenv failed");
    return false;
  }
#endif

  if (LUA_OK != lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    return false;
  }

  return true;
}

bool exec_code_with_env(lua_State *L, const char *codes, int envidx) {
  lua_auto_block autoBlock(L);

  int hmsg = get_pcall_hmsg(L);
  if (LUA_OK != luaL_loadstring(L, codes)) {
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    return false;
  }

  if (envidx < 0) {
    envidx -= 2;
  }
  lua_pushvalue(L, envidx);
  // @see https://www.lua.org/manual/5.3/manual.html#2.2 and load_aux@lbaselib.c in lua source
  // @see https://www.lua.org/manual/5.1/manual.html#lua_setfenv
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM > 501
  if (nullptr == lua_setupvalue(L, -2, 1)) {
    lua_pop(L, 1);
    FWLOGERROR("{}", "lua_setupvalue failed");
    return false;
  }
#else
  if (0 == lua_setfenv(L, -2)) {
    FWLOGERROR("{}", "lua_setfenv failed");
    return false;
  }
#endif

  if (LUA_OK != lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    return false;
  }

  return true;
}

bool exec_file_with_protected_env(lua_State *L, const char *file_path, const char *env_cache_path) {
  lua_auto_block autoBlock(L);

  if (mutable_env_table(L, env_cache_path)) {
    return exec_file_with_env(L, file_path, -1);
  } else {
    return exec_file(L, file_path);
  }
}

bool exec_code_with_protected_env(lua_State *L, const char *codes, const char *env_cache_path) {
  lua_auto_block autoBlock(L);

  if (mutable_env_table(L, env_cache_path)) {
    return exec_code_with_env(L, codes, -1);
  } else {
    return exec_code(L, codes);
  }
}

int mutable_env_table(lua_State *L, const char *env_cache_path) {
  if (L == nullptr) {
    return 0;
  }

  int top = lua_gettop(L);

  if (env_cache_path && *env_cache_path) {
    if (load_item(L, env_cache_path, false)) {
      return 1;
    }
    lua_pop(L, 1);

    if (load_item(L, env_cache_path, true)) {  // table 1
      lua_newtable(L);                         // table 2
      int hmsg = get_pcall_hmsg(L);            // function 3
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM > 501
      if (luaL_loadstring(L, "return _ENV") || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
        // ->| table 1, table 2, function 3, error string
        FWLOGERROR("{}", luaL_checkstring(L, -1));
        lua_settop(L, top);
        return 0;
      }
#else
      if (luaL_loadstring(L, "return _G") || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
        // ->| table 1, table 2, function 3, error string
        FWLOGERROR("{}", luaL_checkstring(L, -1));
        lua_settop(L, top);
        return 0;
      }
#endif
      // ->| table 1, table 2, function 3, table _G or _ENV
      lua_remove(L, -2);  // remove function 3
      lua_setfield(L, -2, "__index");
      lua_setmetatable(L, -2);
      return 1;
    } else {
      lua_pop(L, 1);
    }
  }

  lua_newtable(L);               // table 1
  lua_newtable(L);               // table 2
  int hmsg = get_pcall_hmsg(L);  // function 3
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM > 501
  if (luaL_loadstring(L, "return _ENV") || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
    // ->| table 1, table 2, function 3, error string
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    lua_settop(L, top);
    return 0;
  }
#else
  if (luaL_loadstring(L, "return _G") || lua_pcall(L, 0, LUA_MULTRET, hmsg)) {
    // ->| table 1, table 2, function 3, error string
    FWLOGERROR("{}", luaL_checkstring(L, -1));
    lua_settop(L, top);
    return 0;
  }
#endif
  // ->| table 1, table 2, function 3, table _G or _ENV
  lua_remove(L, -2);  // remove function
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  return 1;
}

int lua_stackdump(lua_State *L) {
  int top = lua_gettop(L);

#if LUA_VERSION_NUM >= 502
  luaL_traceback(L, L, nullptr, 1);
#else
  lua_getglobal(L, "debug");

  lua_getfield(L, -1, "traceback");
  lua_remove(L, -2);

  if (top > 0) {
    lua_pushvalue(L, top);
  } else {
    lua_pushnil(L);
  }
  lua_pushinteger(L, 1);

  if (lua_pcall(L, 2, LUA_MULTRET, 0) != 0) {
    FWLOGERROR("{}", luaL_checkstring(L, -1));
  }
#endif
  return lua_gettop(L) - top;
}

std::string lua_stackdump_to_string(lua_State *L) {
  std::stringstream ss;

  int index = 1;
  lua_Debug ar;
  while (lua_getstack(L, index, &ar)) {
    lua_getinfo(L, "Snl", &ar);

    ss << "    ";

    if (ar.short_src[0]) {
      ss << static_cast<const char *>(ar.short_src);
    }

    if (ar.name) {
      ss << "(" << static_cast<const char *>(ar.name) << ")";
    }

    ss << ":" << ar.currentline << std::endl;

    ++index;
  }

  return ss.str();
}
}  // namespace fn
}  // namespace lua
}  // namespace script
