// Copyright 2026 atframework

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.local.table.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

// lua.h picks dllimport from LUA_BUILD_AS_DLL when consuming the Windows dynamic lua runtime.
#if defined(_WIN32) && !defined(LUA_BUILD_AS_DLL)
#  define LUA_BUILD_AS_DLL 1
#endif

extern "C" {
#include <lua.h>
#include <lualib.h>
}

#include <atframework/testing/mock_db.h>
#include <atframework/testing/runtime.h>

#include "dispatcher/db_msg_dispatcher.h"
#include "frame/test_macros.h"
#include "rpc/db/db_utils.h"
#include "rpc/db/hash_table.h"

namespace {
bool start_db_script_runtime(atfw::testing::runtime &test) {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::db};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }
  test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();
  return true;
}

uint32_t db_script_channel() { return static_cast<uint32_t>(db_msg_dispatcher::me()->get_db_channel_type()); }

const PROJECT_NAMESPACE_ID::table_login_auth &cast_login_auth(
    const atfw::util::memory::strong_rc_ptr<rpc::shared_abstract_message<google::protobuf::Message>> &message) {
  return static_cast<const PROJECT_NAMESPACE_ID::table_login_auth &>(*message->get());
}

// Minimal redis.call simulator covering exactly the commands the db_msg_dispatcher scripts use
// (HGET/HSET/HDEL/HINCRBY/HKEYS). Values are stored as strings like Redis does; a missing key or
// field reads back as false (the Redis nil bulk reply), so tonumber() in the scripts keeps a
// missing version at 0. The __test_* globals expose the simulated hashes to the assertions.
// (Array type: the constructor feeds sizeof(*this) - 1 bytes to the Lua reader.)
constexpr const char kLuaRedisSimulator[] = R"lua(
local __hashes = {}
local function __hash_of(key)
  local hash = __hashes[key]
  if nil == hash then
    hash = {}
    __hashes[key] = hash
  end
  return hash
end

redis = {}
function redis.call(cmd, ...)
  local args = {...}
  if cmd == 'HGET' then
    local hash = __hashes[args[1]]
    if nil == hash or nil == hash[args[2]] then
      return false
    end
    return hash[args[2]]
  elseif cmd == 'HSET' then
    local hash = __hash_of(args[1])
    local added = 0
    local i = 2
    while i + 1 <= #args do
      if nil == hash[args[i]] then
        added = added + 1
      end
      hash[args[i]] = tostring(args[i + 1])
      i = i + 2
    end
    return added
  elseif cmd == 'HINCRBY' then
    local hash = __hash_of(args[1])
    local current = (tonumber(hash[args[2]]) or 0) + tonumber(args[3])
    hash[args[2]] = tostring(current)
    return current
  elseif cmd == 'HDEL' then
    local hash = __hashes[args[1]]
    local removed = 0
    if nil ~= hash then
      for i = 2, #args do
        if nil ~= hash[args[i]] then
          removed = removed + 1
          hash[args[i]] = nil
        end
      end
    end
    return removed
  elseif cmd == 'HKEYS' then
    local hash = __hashes[args[1]]
    local fields = {}
    if nil ~= hash then
      for field in pairs(hash) do
        fields[#fields + 1] = field
      end
    end
    return fields
  else
    error('unsupported redis command: ' .. cmd)
  end
end

function __test_hset(key, field, value)
  local hash = __hash_of(key)
  hash[field] = value
  return true
end

function __test_hget(key, field)
  local hash = __hashes[key]
  if nil == hash or nil == hash[field] then
    return false
  end
  return hash[field]
end

function __test_hfield_count(key)
  local hash = __hashes[key]
  if nil == hash then
    return 0
  end
  local count = 0
  for _ in pairs(hash) do
    count = count + 1
  end
  return count
end
)lua";

// Executes the real db_msg_dispatcher Lua scripts the way Redis does (KEYS/ARGV globals, the
// script's returned {ok=...}/{err=...} table acting as the status/error reply). Uses only the lua
// core C API: the installed third-party lua ships lua.h/lualib.h without lauxlib.h, so chunks are
// loaded through a lua_load reader and the state gets a plain realloc allocator plus the base and
// table standard libraries (tonumber/tostring/pairs/ipairs/table.unpack/error cover the scripts).
class lua_script_engine {
 public:
  struct script_reply {
    // { ok = ... } (status reply, e.g. the new CAS version) vs { err = ... } (error reply, e.g.
    // "CAS_FAILED|<stored version>"), exactly the two shapes the scripts return.
    bool is_ok = false;
    std::string value;
  };

  lua_script_engine() {
#if LUA_VERSION_NUM >= 505
    lua_state_ = lua_newstate(&lua_script_engine::allocator, nullptr, 0);
#else
    lua_state_ = lua_newstate(&lua_script_engine::allocator, nullptr);
#endif
    if (nullptr == lua_state_) {
      last_error_ = "lua_newstate failed";
      return;
    }

    // Only the base and table libraries are needed (tonumber/tostring/pairs/ipairs/error plus
    // table.unpack for the scripts). luaopen_base registers its functions into _G itself (Lua 5.5
    // no longer defines LUA_BASELIBNAME), while the other libraries just return the module table,
    // so it must be stored into its global name like luaL_requiref does.
    lua_pushcfunction(lua_state_, &luaopen_base);
    lua_pushlstring(lua_state_, "_G", 2);
    lua_call(lua_state_, 1, 0);
    lua_pushcfunction(lua_state_, &luaopen_table);
    lua_pushlstring(lua_state_, LUA_TABLIBNAME, sizeof(LUA_TABLIBNAME) - 1);
    lua_call(lua_state_, 1, 1);
    lua_setglobal(lua_state_, LUA_TABLIBNAME);

    if (!load_chunk(kLuaRedisSimulator, sizeof(kLuaRedisSimulator) - 1, "redis-simulator")) {
      return;
    }
    if (0 != lua_pcall(lua_state_, 0, 0, 0)) {
      last_error_ = stack_to_string(-1);
      lua_settop(lua_state_, 0);
      return;
    }
    lua_settop(lua_state_, 0);
    available_ = true;
  }

  ~lua_script_engine() {
    if (nullptr != lua_state_) {
      lua_close(lua_state_);
      lua_state_ = nullptr;
    }
  }

  lua_script_engine(const lua_script_engine &) = delete;
  lua_script_engine &operator=(const lua_script_engine &) = delete;

  bool is_available() const noexcept { return available_; }
  const std::string &get_last_error() const noexcept { return last_error_; }

  bool run_script(const char *script, const std::string &key, const std::vector<std::string> &argv,
                  script_reply &reply) {
    if (!available_ || nullptr == script) {
      return false;
    }
    lua_settop(lua_state_, 0);
    if (!load_chunk(script, std::strlen(script), "db-script")) {
      return false;
    }

    lua_createtable(lua_state_, 1, 0);
    lua_pushlstring(lua_state_, key.data(), key.size());
    lua_rawseti(lua_state_, -2, 1);
    lua_setglobal(lua_state_, "KEYS");

    lua_createtable(lua_state_, static_cast<int>(argv.size()), 0);
    for (size_t i = 0; i < argv.size(); ++i) {
      lua_pushlstring(lua_state_, argv[i].data(), argv[i].size());
      lua_rawseti(lua_state_, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setglobal(lua_state_, "ARGV");

    if (0 != lua_pcall(lua_state_, 0, 1, 0)) {
      last_error_ = stack_to_string(-1);
      lua_settop(lua_state_, 0);
      return false;
    }
    if (!lua_istable(lua_state_, -1)) {
      last_error_ = "script reply is not a table";
      lua_settop(lua_state_, 0);
      return false;
    }

    lua_getfield(lua_state_, -1, "ok");
    if (!lua_isnil(lua_state_, -1)) {
      reply.is_ok = true;
      reply.value = stack_to_string(-1);
    } else {
      lua_pop(lua_state_, 1);
      lua_getfield(lua_state_, -1, "err");
      reply.is_ok = false;
      reply.value = stack_to_string(-1);
    }
    lua_settop(lua_state_, 0);
    return true;
  }

  // Present-ness + value of one field in the simulated hash.
  bool hget(const std::string &key, const std::string &field, std::string &value) {
    value.clear();
    if (!available_) {
      return false;
    }
    lua_settop(lua_state_, 0);
    lua_getglobal(lua_state_, "__test_hget");
    lua_pushlstring(lua_state_, key.data(), key.size());
    lua_pushlstring(lua_state_, field.data(), field.size());
    if (0 != lua_pcall(lua_state_, 2, 1, 0)) {
      last_error_ = stack_to_string(-1);
      lua_settop(lua_state_, 0);
      return false;
    }
    bool present = !lua_isboolean(lua_state_, -1);
    if (present) {
      value = stack_to_string(-1);
    }
    lua_settop(lua_state_, 0);
    return present;
  }

  int hfield_count(const std::string &key) {
    if (!available_) {
      return -1;
    }
    lua_settop(lua_state_, 0);
    lua_getglobal(lua_state_, "__test_hfield_count");
    lua_pushlstring(lua_state_, key.data(), key.size());
    if (0 != lua_pcall(lua_state_, 1, 1, 0)) {
      last_error_ = stack_to_string(-1);
      lua_settop(lua_state_, 0);
      return -1;
    }
    int count = static_cast<int>(lua_tointeger(lua_state_, -1));
    lua_settop(lua_state_, 0);
    return count;
  }

  bool hset_direct(const std::string &key, const std::string &field, const std::string &value) {
    if (!available_) {
      return false;
    }
    lua_settop(lua_state_, 0);
    lua_getglobal(lua_state_, "__test_hset");
    lua_pushlstring(lua_state_, key.data(), key.size());
    lua_pushlstring(lua_state_, field.data(), field.size());
    lua_pushlstring(lua_state_, value.data(), value.size());
    if (0 != lua_pcall(lua_state_, 3, 0, 0)) {
      last_error_ = stack_to_string(-1);
      lua_settop(lua_state_, 0);
      return false;
    }
    lua_settop(lua_state_, 0);
    return true;
  }

 private:
  static void *allocator(void *, void *ptr, size_t, size_t new_size) {
    if (0 == new_size) {
      std::free(ptr);
      return nullptr;
    }
    return std::realloc(ptr, new_size);
  }

  struct chunk_source_t {
    const char *data;
    size_t size;
  };

  static const char *chunk_reader(lua_State *, void *userdata, size_t *size) {
    chunk_source_t *source = static_cast<chunk_source_t *>(userdata);
    if (nullptr == source || 0 == source->size) {
      *size = 0;
      return nullptr;
    }
    *size = source->size;
    source->size = 0;
    return source->data;
  }

  // Leaves the loaded chunk on the stack (caller is responsible for the stack afterwards).
  bool load_chunk(const char *code, size_t code_size, const char *name) {
    chunk_source_t source{code, code_size};
#if LUA_VERSION_NUM >= 502
    int res = lua_load(lua_state_, &lua_script_engine::chunk_reader, &source, name, nullptr);
#else
    int res = lua_load(lua_state_, &lua_script_engine::chunk_reader, &source, name);
#endif
    if (0 != res) {
      last_error_ = stack_to_string(-1);
      lua_settop(lua_state_, 0);
      return false;
    }
    return true;
  }

  std::string stack_to_string(int index) {
    size_t len = 0;
    const char *str = lua_tolstring(lua_state_, index, &len);
    if (nullptr == str) {
      return std::string{};
    }
    return std::string{str, len};
  }

  lua_State *lua_state_ = nullptr;
  bool available_ = false;
  std::string last_error_;
};

// Build ARGV exactly like the versioned production path does: rpc::db::pack_message pushes
// RPC_DB_VERSION_NAME + the expected version first, then one name/value pair per present field
// ('&'-prefixed string/message values, decimal integers).
std::vector<std::string> make_versioned_argv(uint64_t expected_version,
                                             std::initializer_list<std::pair<std::string, std::string>> fields) {
  std::vector<std::string> argv{RPC_DB_VERSION_NAME, std::to_string(expected_version)};
  for (const auto &field : fields) {
    argv.push_back(field.first);
    argv.push_back(field.second);
  }
  return argv;
}

// run_script with the engine diagnostic attached to the assertion.
bool run_script_checked(lua_script_engine &lua, const char *script, const std::string &key,
                        const std::vector<std::string> &argv, lua_script_engine::script_reply &reply) {
  if (!lua.run_script(script, key, argv, reply)) {
    CASE_MSG_INFO() << "run_script failed: " << lua.get_last_error() << '\n';
    return false;
  }
  return true;
}
}  // namespace

// The Lua scripts embedded in db_msg_dispatcher (kCompareAndSetHashTable, kInsertHashTable,
// kAddListIndexHashTable) are the authoritative write contract of the hash-table backend. The
// db_lua_script_* cases below execute the real scripts in an embedded interpreter against a
// redis.call simulator, with KEYS/ARGV shaped exactly like the production commands; the
// db_script_* cases pin the same contract through the rpc::db::hash_table primitives, and mock_db
// (the offline in-memory mirror) must satisfy them too. When a script changes, re-verify these
// cases and update mock_db together.

// server_frame component: kCompareAndSetHashTable and kInsertHashTable script behavior. A record
// without a version accepts any expected version and starts the sequence at 1; a matching expected
// version bumps the stored version by one; expected version 0 ignores the CAS check and
// force-overwrites (the stored version still bumps from the real one); any other expected version
// fails with CAS_FAILED|<stored version> without touching the record; the insert script only
// accepts a record that carries no version field.
CASE_TEST(server_frame_unit_test, db_lua_script_cas_and_insert_behavior) {
  using script_type = db_msg_dispatcher::script_type;
  const char *cas_script = db_msg_dispatcher::get_db_script_source(script_type::kCompareAndSetHashTable);
  const char *insert_script = db_msg_dispatcher::get_db_script_source(script_type::kInsertHashTable);
  CASE_EXPECT_TRUE(nullptr != cas_script);
  CASE_EXPECT_TRUE(nullptr != insert_script);
  if (nullptr == cas_script || nullptr == insert_script) {
    return;
  }

  lua_script_engine lua;
  if (!lua.is_available()) {
    CASE_MSG_INFO() << "lua engine unavailable: " << lua.get_last_error() << '\n';
    CASE_EXPECT_TRUE(false);
    return;
  }

  const std::string key = "unit-test:lua:cas";
  lua_script_engine::script_reply reply;
  std::string field_value;

  // Missing record, expected version 0: accepted, the CAS sequence starts at 1, and the version
  // field plus the ARGV data fields are written.
  CASE_EXPECT_TRUE(run_script_checked(lua, cas_script, key, make_versioned_argv(0, {{"open_id", "&alice"}}), reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("1", reply.value);
  CASE_EXPECT_TRUE(lua.hget(key, RPC_DB_VERSION_NAME, field_value));
  CASE_EXPECT_EQ("1", field_value);
  CASE_EXPECT_TRUE(lua.hget(key, "open_id", field_value));
  CASE_EXPECT_EQ("&alice", field_value);
  CASE_EXPECT_EQ(2, lua.hfield_count(key));

  // Matching expected version (the one the previous write returned): applied, the stored version
  // bumps by one (1 -> 2), and only the fields present in ARGV are written (HSET merge keeps
  // open_id).
  CASE_EXPECT_TRUE(run_script_checked(lua, cas_script, key, make_versioned_argv(1, {{"user_id", "1001"}}), reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("2", reply.value);
  CASE_EXPECT_TRUE(lua.hget(key, "open_id", field_value));
  CASE_EXPECT_EQ("&alice", field_value);
  CASE_EXPECT_EQ(3, lua.hfield_count(key));

  // Expected version 0 with a versioned record: the CAS check is ignored and the record is
  // force-overwritten; the stored version still bumps from the real one (2 -> 3), never from the
  // expected one.
  CASE_EXPECT_TRUE(run_script_checked(lua, cas_script, key, make_versioned_argv(0, {{"user_id", "1002"}}), reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("3", reply.value);
  CASE_EXPECT_TRUE(lua.hget(key, "user_id", field_value));
  CASE_EXPECT_EQ("1002", field_value);
  CASE_EXPECT_TRUE(lua.hget(key, RPC_DB_VERSION_NAME, field_value));
  CASE_EXPECT_EQ("3", field_value);

  // Stale expected version (1 while 3 is stored): the reply is the error the dispatcher maps to
  // EN_DB_OLD_VERSION, carrying the stored version after "CAS_FAILED|" (parsed from offset 11),
  // and the record is untouched.
  CASE_EXPECT_TRUE(run_script_checked(lua, cas_script, key, make_versioned_argv(1, {{"user_id", "9999"}}), reply));
  CASE_EXPECT_FALSE(reply.is_ok);
  CASE_EXPECT_EQ("CAS_FAILED|3", reply.value);
  CASE_EXPECT_TRUE(lua.hget(key, "user_id", field_value));
  CASE_EXPECT_EQ("1002", field_value);
  CASE_EXPECT_TRUE(lua.hget(key, RPC_DB_VERSION_NAME, field_value));
  CASE_EXPECT_EQ("3", field_value);

  // A record that only carries unversioned data (no version field in the hash) still reads
  // version 0 for the script: any expected version is accepted and the sequence starts at 1.
  const std::string cas_unversioned_key = "unit-test:lua:cas-unversioned";
  CASE_EXPECT_TRUE(lua.hset_direct(cas_unversioned_key, "open_id", "&bob"));
  CASE_EXPECT_TRUE(
      run_script_checked(lua, cas_script, cas_unversioned_key, make_versioned_argv(7, {{"user_id", "2001"}}), reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("1", reply.value);

  // kInsertHashTable: accepted on a missing key (the version starts at 1)...
  const std::string insert_key = "unit-test:lua:insert";
  CASE_EXPECT_TRUE(
      run_script_checked(lua, insert_script, insert_key, make_versioned_argv(0, {{"open_id", "&carol"}}), reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("1", reply.value);

  // ...rejected on a record that carries a version, with the stored version reported back and the
  // record left untouched (the caller maps this to EN_DB_KEY_EXISTS)...
  CASE_EXPECT_TRUE(
      run_script_checked(lua, insert_script, insert_key, make_versioned_argv(0, {{"open_id", "&dave"}}), reply));
  CASE_EXPECT_FALSE(reply.is_ok);
  CASE_EXPECT_EQ("CAS_FAILED|1", reply.value);
  CASE_EXPECT_TRUE(lua.hget(insert_key, "open_id", field_value));
  CASE_EXPECT_EQ("&carol", field_value);

  // ...and accepted into a record that only carries unversioned data.
  const std::string insert_unversioned_key = "unit-test:lua:insert-unversioned";
  CASE_EXPECT_TRUE(lua.hset_direct(insert_unversioned_key, "open_id", "&eve"));
  CASE_EXPECT_TRUE(run_script_checked(lua, insert_script, insert_unversioned_key,
                                      make_versioned_argv(0, {{"user_id", "3001"}}), reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("1", reply.value);
}

// server_frame component: kAddListIndexHashTable script behavior. Indexes are 1-based (HINCRBY on
// a missing counter) and monotonic; the counter field must be "__index_number"
// (REDIS_LIST_INDEX_FIELD in rpc/db/db_utils.cpp, which the unpack path skips when reading a list
// back); at capacity (fields >= max_len + 1, the counter included) the smallest data index is
// evicted before the new one is appended and evicted indexes are never reused; max_len 0 keeps
// exactly one data entry.
CASE_TEST(server_frame_unit_test, db_lua_script_kl_add_index_behavior) {
  const char *script = db_msg_dispatcher::get_db_script_source(db_msg_dispatcher::script_type::kAddListIndexHashTable);
  CASE_EXPECT_TRUE(nullptr != script);
  if (nullptr == script) {
    return;
  }

  lua_script_engine lua;
  if (!lua.is_available()) {
    CASE_MSG_INFO() << "lua engine unavailable: " << lua.get_last_error() << '\n';
    CASE_EXPECT_TRUE(false);
    return;
  }

  const std::string key = "unit-test:lua:kl";
  lua_script_engine::script_reply reply;
  std::string field_value;

  for (int i = 1; i <= 3; ++i) {
    CASE_EXPECT_TRUE(run_script_checked(lua, script, key, {"3", "&entry-" + std::to_string(i)}, reply));
    CASE_EXPECT_TRUE(reply.is_ok);
    CASE_EXPECT_EQ(std::to_string(i), reply.value);
  }
  // The monotonic counter lives in REDIS_LIST_INDEX_FIELD ("__index_number"): counter + 3 data
  // fields.
  CASE_EXPECT_TRUE(lua.hget(key, "__index_number", field_value));
  CASE_EXPECT_EQ("3", field_value);
  CASE_EXPECT_EQ(4, lua.hfield_count(key));

  // At capacity (4 fields >= max_len + 1 = 4): the smallest data index is evicted before the next
  // monotonic index is appended.
  CASE_EXPECT_TRUE(run_script_checked(lua, script, key, {"3", "&entry-4"}, reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("4", reply.value);
  CASE_EXPECT_FALSE(lua.hget(key, "1", field_value));
  CASE_EXPECT_TRUE(lua.hget(key, "2", field_value));
  CASE_EXPECT_EQ("&entry-2", field_value);
  CASE_EXPECT_TRUE(lua.hget(key, "4", field_value));
  CASE_EXPECT_EQ("&entry-4", field_value);
  CASE_EXPECT_EQ(4, lua.hfield_count(key));

  // The evicted index is never reused: the next add evicts index 2 and allocates 5.
  CASE_EXPECT_TRUE(run_script_checked(lua, script, key, {"3", "&entry-5"}, reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("5", reply.value);
  CASE_EXPECT_FALSE(lua.hget(key, "2", field_value));
  CASE_EXPECT_TRUE(lua.hget(key, "5", field_value));
  CASE_EXPECT_EQ("&entry-5", field_value);

  // max_len 0: the counter field counts against the budget, so exactly one data entry is kept and
  // every add replaces the previous one.
  const std::string solo_key = "unit-test:lua:kl-solo";
  CASE_EXPECT_TRUE(run_script_checked(lua, script, solo_key, {"0", "&solo-1"}, reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("1", reply.value);
  CASE_EXPECT_EQ(2, lua.hfield_count(solo_key));
  CASE_EXPECT_TRUE(run_script_checked(lua, script, solo_key, {"0", "&solo-2"}, reply));
  CASE_EXPECT_TRUE(reply.is_ok);
  CASE_EXPECT_EQ("2", reply.value);
  CASE_EXPECT_FALSE(lua.hget(solo_key, "1", field_value));
  CASE_EXPECT_TRUE(lua.hget(solo_key, "2", field_value));
  CASE_EXPECT_EQ("&solo-2", field_value);
  CASE_EXPECT_EQ(2, lua.hfield_count(solo_key));
}

// server_frame component: kCompareAndSetHashTable behavior contract through the production
// rpc::db::hash_table primitives (mock_db must satisfy the same contract). A read reports the
// stored version; an update carrying exactly that version is applied and bumps the stored version
// by one; an expected version of 0 ignores the CAS check and force-overwrites (the stored version
// still bumps from the real one); any other non-zero expected version fails with
// EN_DB_OLD_VERSION, writes the stored version back and leaves the record untouched.
CASE_TEST(server_frame_unit_test, db_script_cas_version_contract) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_script_cas", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    const uint32_t channel = db_script_channel();
    const gsl::string_view key = "ut:kv:script-cas";
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> record{ctx};
    record->set_open_id("openid-script-cas");
    record->set_user_id(1001);

    // A missing record accepts any expected version and starts the CAS sequence at 1.
    uint64_t version = 0;
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    // The read reports exactly the version the write returned (read/write version consistency).
    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1001, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // An update carrying the version the read reported is applied, and the stored version really
    // changes: 1 -> 2.
    record->set_user_id(1002);
    version = output->version;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1002, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // A stale expected version (1 while 2 is stored): rejected with EN_DB_OLD_VERSION, the stored
    // version is written back and the conflicting write does not land.
    record->set_user_id(9999);
    version = 1;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, res);
    CASE_EXPECT_EQ(2, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(2, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1002, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // Expected version 0 ignores the CAS check: the versioned record is overwritten regardless of
    // its stored version, the write lands and the stored version still bumps from the real one
    // (2 -> 3), never from the expected one.
    record->set_user_id(1003);
    version = 0;
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(3, static_cast<int>(output->version));
    if (output->message) {
      CASE_EXPECT_EQ(1003, static_cast<int>(cast_login_auth(output->message).user_id()));
    } else {
      CASE_EXPECT_TRUE(false);
    }
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(3, static_cast<int>(test.db().get_version("ut:kv:script-cas")));
  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: kInsertHashTable behavior contract through the production primitives.
// Only a key without a stored CAS version can be inserted; a duplicate fails with
// EN_DB_KEY_EXISTS (the caller's mapping of the script's CAS_FAILED reply), reports the stored
// version and leaves the record untouched; a record written only by the unversioned set still
// counts as absent for the script and accepts the insert.
CASE_TEST(server_frame_unit_test, db_script_insert_only_contract) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task =
      test.run_task("db_script_insert", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        const uint32_t channel = db_script_channel();
        const gsl::string_view key = "ut:kv:script-insert";
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> record{ctx};
        record->set_open_id("openid-script-insert");
        record->set_user_id(2001);

        uint64_t version = 0;
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
            ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));

        // The record now carries a CAS version: the script reports CAS_FAILED, which the caller
        // maps to EN_DB_KEY_EXISTS with the stored version written back.
        record->set_user_id(2002);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
            ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, version));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));

        auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output->version));
        if (output->message) {
          CASE_EXPECT_EQ(2001, static_cast<int>(cast_login_auth(output->message).user_id()));
        } else {
          CASE_EXPECT_TRUE(false);
        }

        // A record written only by the unversioned set has no CAS_VERSION field, so the script still
        // sees version 0 and the insert is accepted into it (the version sequence starts at 1).
        const gsl::string_view unversioned_key = "ut:kv:script-insert-unversioned";
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> unversioned{ctx};
        unversioned->set_open_id("openid-script-insert-unversioned");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
            ctx, channel, unversioned_key, rpc::shared_abstract_message<google::protobuf::Message>{unversioned},
            nullptr));
        CASE_EXPECT_EQ(0, res);

        unversioned->set_user_id(3001);
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::insert(
            ctx, channel, unversioned_key, rpc::shared_abstract_message<google::protobuf::Message>{unversioned},
            version));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(version));
        RPC_RETURN_CODE(0);
      });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: every write path (plain HSET, the CAS script and the insert script) HSETs
// only the fields present in the store message, so fields absent from a partial store survive.
CASE_TEST(server_frame_unit_test, db_script_set_merges_present_fields) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task = test.run_task("db_script_merge", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    const uint32_t channel = db_script_channel();
    const gsl::string_view key = "ut:kv:script-merge";
    auto output = atfw::component::memory::stl::make_strong_rc<db_key_value_message_result_t>();

    // Unversioned HSET: a partial store keeps the fields of the previous one.
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> record{ctx};
    record->set_open_id("openid-script-merge");
    record->set_user_id(4001);
    record->set_access_token_code("token-1");
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{record}, nullptr));
    CASE_EXPECT_EQ(0, res);

    rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> partial{ctx};
    partial->set_user_id(4002);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{partial}, nullptr));
    CASE_EXPECT_EQ(0, res);

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    if (output->message) {
      const auto &restored = cast_login_auth(output->message);
      CASE_EXPECT_EQ(4002, static_cast<int>(restored.user_id()));
      CASE_EXPECT_EQ("openid-script-merge", restored.open_id());
      CASE_EXPECT_EQ("token-1", restored.access_token_code());
    } else {
      CASE_EXPECT_TRUE(false);
    }

    // The versioned CAS write merges the same way (the script HSETs the packed ARGV fields only):
    // bumping user_id alone must not drop open_id/access_token_code, and the version the read
    // reported stays usable for the next update.
    uint64_t version = 0;
    partial->set_user_id(4003);
    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::set(
        ctx, channel, key, rpc::shared_abstract_message<google::protobuf::Message>{partial}, &version));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(version));

    res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_value::get_all(ctx, channel, key, output, nullptr));
    CASE_EXPECT_EQ(0, res);
    CASE_EXPECT_EQ(1, static_cast<int>(output->version));
    if (output->message) {
      const auto &restored = cast_login_auth(output->message);
      CASE_EXPECT_EQ(4003, static_cast<int>(restored.user_id()));
      CASE_EXPECT_EQ("openid-script-merge", restored.open_id());
      CASE_EXPECT_EQ("token-1", restored.access_token_code());
    } else {
      CASE_EXPECT_TRUE(false);
    }
    RPC_RETURN_CODE(0);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(0, test.stop());
}

// server_frame component: kAddListIndexHashTable behavior contract through the production
// primitives. Indexes are 1-based, monotonic and never reused; update_by_index rewrites an entry
// in place without allocating an index; at capacity (data entries >= max_list_length, the counter
// field counts against the budget in the script) exactly one entry — the smallest index — is
// evicted before the new one is appended; max_list_length 0 keeps a single entry.
CASE_TEST(server_frame_unit_test, db_script_kl_add_index_contract) {
  atfw::testing::runtime test;
  if (!start_db_script_runtime(test)) {
    return;
  }

  auto task =
      test.run_task("db_script_kl_add", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
        const uint32_t channel = db_script_channel();
        const gsl::string_view key = "ut:kl:script-add";
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> entry{ctx};
        std::vector<db_key_list_message_result_t> output;

        // First allocated index is 1 and the sequence is monotonic.
        entry->set_access_token_code("one");
        int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, channel, key, 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);
        entry->set_access_token_code("two");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, channel, key, 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);
        entry->set_access_token_code("three");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, channel, key, 10, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, channel, key, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(3, static_cast<int>(output.size()));
        for (const auto &item : output) {
          CASE_EXPECT_TRUE(item.list_index >= 1 && item.list_index <= 3);
        }

        // update_by_index rewrites the entry in place: same index, new data, no index allocated.
        entry->set_access_token_code("one-v2");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::update_by_index(
            ctx, channel, key, 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);
        uint64_t updated[] = {1};
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(
            ctx, channel, key, gsl::span<uint64_t>{updated}, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
        if (output.size() == 1) {
          CASE_EXPECT_EQ(1, static_cast<int>(output[0].list_index));
          if (output[0].message) {
            CASE_EXPECT_EQ("one-v2", cast_login_auth(output[0].message).access_token_code());
          }
        }

        // Reorder so the smallest index is not at the oldest insertion position: remove index 1, then
        // re-create it via update_by_index (appended last).
        uint64_t removed[] = {1};
        res = RPC_AWAIT_CODE_RESULT(
            rpc::db::hash_table::key_list::remove_by_index(ctx, channel, key, gsl::span<uint64_t>{removed}));
        CASE_EXPECT_EQ(0, res);

        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::update_by_index(
            ctx, channel, key, 1, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        // At capacity (3 entries, max_list_length 3) the add evicts the smallest index (1), not the
        // oldest insertion position, and appends a fresh monotonic index (4) — the evicted index is
        // never reused.
        entry->set_access_token_code("four");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, channel, key, 3, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        uint64_t probe[] = {1, 2, 3, 4};
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_by_indexs(
            ctx, channel, key, gsl::span<uint64_t>{probe}, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(4, static_cast<int>(output.size()));
        if (output.size() == 4) {
          CASE_EXPECT_FALSE(!!output[0].message);  // 1: evicted and never reused
          CASE_EXPECT_TRUE(!!output[1].message);   // 2
          CASE_EXPECT_TRUE(!!output[2].message);   // 3
          CASE_EXPECT_TRUE(!!output[3].message);   // 4: appended
          if (output[3].message) {
            CASE_EXPECT_EQ("four", cast_login_auth(output[3].message).access_token_code());
          }
        }

        // max_list_length 0: the counter field always counts against the budget, so the list keeps a
        // single entry and every add replaces the previous one.
        const gsl::string_view max0_key = "ut:kl:script-max0";
        entry->set_access_token_code("solo-1");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, channel, max0_key, 0, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);
        entry->set_access_token_code("solo-2");
        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::add_index(
            ctx, channel, max0_key, 0, rpc::shared_abstract_message<google::protobuf::Message>{entry}));
        CASE_EXPECT_EQ(0, res);

        res = RPC_AWAIT_CODE_RESULT(rpc::db::hash_table::key_list::get_all(ctx, channel, max0_key, output, nullptr));
        CASE_EXPECT_EQ(0, res);
        CASE_EXPECT_EQ(1, static_cast<int>(output.size()));
        if (output.size() == 1 && output[0].message) {
          CASE_EXPECT_EQ("solo-2", cast_login_auth(output[0].message).access_token_code());
          CASE_EXPECT_EQ(2, static_cast<int>(output[0].list_index));
        }
        RPC_RETURN_CODE(0);
      });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);

  CASE_EXPECT_EQ(6, static_cast<int>(test.db().calls(atfw::testing::mock_db::op_type::kl_add_index)));
  CASE_EXPECT_EQ(0, test.stop());
}
