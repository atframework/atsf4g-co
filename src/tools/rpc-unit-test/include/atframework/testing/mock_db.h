// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/message.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/runtime.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rpc/db/hash_table.h"
#include "rpc/rpc_shared_message.h"

namespace atframework {
namespace testing {

class mock_db;

// One recorded hash_table operation.
struct ATFW_UTIL_SYMBOL_VISIBLE db_request_record {
  rpc::db::hash_table::unit_test_request::op_type op = rpc::db::hash_table::unit_test_request::op_type::kv_get_all;
  std::string key;
  // Table name extracted from the key ("{record_prefix}-{table_name}.{fields}"); empty when the key
  // does not follow the generated layout.
  std::string table_name;
  int32_t result_code = 0;
};

class mock_db;

// Callback context of one intercepted hash_table operation on a per-table mock rule. The handler may
// inspect the request inputs, set return_code, fill the returned record (output_table + output_version
// for kv reads, kl_output entries for kl reads, inc_message for kv_inc_field) and decide the outcome:
// returning true means the operation is handled and the common mock layer (in-memory backend) is
// skipped entirely; returning false falls through to the common mock layer.
struct ATFW_UTIL_SYMBOL_VISIBLE db_table_context {
  using op_type = rpc::db::hash_table::unit_test_request::op_type;

  op_type op = op_type::kv_get_all;
  gsl::string_view key;
  gsl::string_view table_name;
  rpc::context *rpc_context = nullptr;

  // Inputs (empty/nullptr when unused by the op).
  const google::protobuf::Message *input_table = nullptr;  // kv_set: the record being stored
  gsl::span<const gsl::string_view> partly_get_fields{};   // kv_partly_get: requested fields
  gsl::string_view inc_field;                              // kv_inc_field: target field name
  gsl::span<const uint64_t> list_index{};                  // kl_get_by_indexs/kl_update_by_index/kl_remove_by_index
  uint32_t max_list_length = 0;                            // kl ops
  uint64_t ttl_second = 0;                                 // set_ttl

  // Outputs.
  int32_t return_code = 0;  // function-level result code reported to the caller
  // kv_get_all/kv_partly_get: the returned record; set it with mock_db::make_output (empty means the
  // handler decided to report no record, e.g. together with an error return_code).
  atfw::util::memory::strong_rc_ptr<rpc::shared_abstract_message<google::protobuf::Message>> output_table;
  // kv read result CAS version; also written back to the caller's version pointer when requested.
  uint64_t output_version = 0;
  // kl_get_all/kl_get_by_indexs: append entries here (same vector the caller receives); use
  // mock_db::make_kl_entry to build entries.
  std::vector<db_key_list_message_result_t> *kl_output = nullptr;
  // kv_inc_field: in/out message whose inc_field is mutated by the handler.
  google::protobuf::Message *inc_message = nullptr;
};

// Per-table, per-interface mock handler. Returns true when the operation is handled (the context outputs
// decide the outcome), false to defer this single operation to the common mock layer.
using db_table_handler = std::function<bool(db_table_context &)>;

class mock_db;

namespace detail {
struct db_table_rule_state {
  std::string table_name;
  std::unordered_map<int32_t, db_table_handler> op_handlers;
  db_table_handler any_handler;
  bool active = true;
};
}  // namespace detail

// RAII handle of one per-table mock rule with per-interface callbacks. Register handlers with on() for a
// single interface or on_any() as the table-level catch-all; operations without a matching callback fall
// through to the common mock layer (in-memory backend). The rule is disabled when the handle is
// destroyed; when several active rules match one table, the latest registered one wins.
class RPC_UNIT_TEST_API db_table_rule_handle {
 public:
  using op_type = rpc::db::hash_table::unit_test_request::op_type;

  db_table_rule_handle() = default;
  ~db_table_rule_handle();

  db_table_rule_handle(const db_table_rule_handle &) = delete;
  db_table_rule_handle &operator=(const db_table_rule_handle &) = delete;
  db_table_rule_handle(db_table_rule_handle &&other) noexcept;
  db_table_rule_handle &operator=(db_table_rule_handle &&other) noexcept;

  // Register/replace the callback of one interface (e.g. op_type::kv_get_all).
  db_table_rule_handle &on(op_type op, db_table_handler fn);
  // Register/replace the table-level catch-all, used when no per-interface callback matches.
  db_table_rule_handle &on_any(db_table_handler fn);
  db_table_rule_handle &clear_handlers();

  void reset();
  explicit operator bool() const noexcept { return !!rule_; }

 private:
  friend class mock_db;
  explicit db_table_rule_handle(std::shared_ptr<detail::db_table_rule_state> rule);

  std::shared_ptr<detail::db_table_rule_state> rule_;
};
// In-memory mock of the rpc::db::hash_table backend. Installed through the server_frame unit-test
// hook so every primitive operation (and therefore batch operations and generated db helpers such
// as rpc::db::uuid_allocator) is served synchronously without a real Redis or DB dispatcher init.
//
// Dispatch order: per-table rules registered with mock_table() intercept matching operations through
// per-interface callbacks first; only operations without a matching callback reach this common mock
// layer (the in-memory backend below).
//
// Data model mirrors the real backend contract:
// - values are stored as serialized protobuf bytes (presence of explicitly set fields survives)
// - key_value records carry a CAS version (RPC_DB_VERSION_NAME), incremented by versioned set
// - key_list records use a monotonic per-key index allocator and trim to max_list_length
// - TTL is evaluated lazily against a controllable clock
//
// Get-like operations materialize outputs through message factories registered per protobuf
// full name (register_message_type<T>()). A missing factory fails fast with EN_SYS_UNPACK.
class RPC_UNIT_TEST_API mock_db {
 public:
  using clock = std::chrono::system_clock;
  using message_factory_t = std::function<rpc::shared_abstract_message<google::protobuf::Message>(rpc::context &)>;
  using op_type = rpc::db::hash_table::unit_test_request::op_type;

  mock_db();
  ~mock_db();

  mock_db(const mock_db &) = delete;
  mock_db &operator=(const mock_db &) = delete;

  bool is_active() const noexcept;

  // Register a factory used to materialize stored bytes into rpc messages of one protobuf type.
  void register_message_factory(gsl::string_view full_name, message_factory_t factory);
  template <class TMESSAGE>
  ATFW_UTIL_SYMBOL_VISIBLE void register_message_type() {
    const absl::string_view full_name = TMESSAGE::descriptor()->full_name();
    register_message_factory(gsl::string_view{full_name.data(), full_name.size()}, [](rpc::context &ctx) {
      rpc::shared_message<TMESSAGE> concrete{ctx};
      // The instance is created lazily; materialize it before converting into the abstract wrapper.
      (void)concrete.get();
      return rpc::shared_abstract_message<google::protobuf::Message>{std::move(concrete)};
    });
  }

  // Clock control. Until set_now() is called the real system clock is used.
  void set_now(clock::time_point now);
  void advance(clock::duration offset);
  clock::time_point now() const;

  // State inspection and manipulation for arrange/assert.
  bool has_key(gsl::string_view key) const;
  size_t key_count() const;
  uint64_t get_version(gsl::string_view key) const;
  void erase_key(gsl::string_view key);
  void clear();

  // Raw entry access for arrange/assert and for the generated typed <db namespace>::mock helpers.
  // These read/write the backend directly: no RPC, no task context required. Getters apply lazy
  // TTL expiry the same way as the operation handlers.
  struct ATFW_UTIL_SYMBOL_VISIBLE kv_raw_entry {
    std::string type_name;
    std::string data;
    uint64_t version = 0;
    bool has_expire = false;
    clock::time_point expire_at{};
  };
  struct ATFW_UTIL_SYMBOL_VISIBLE kl_raw_entry {
    uint64_t index = 0;
    std::string type_name;
    std::string data;
  };
  bool get_raw_kv(gsl::string_view key, kv_raw_entry &output);
  void set_raw_kv(gsl::string_view key, gsl::string_view type_name, gsl::string_view data, uint64_t version = 0);
  bool get_raw_kl(gsl::string_view key, std::vector<kl_raw_entry> &output);
  // Append one entry with the per-key monotonic index allocator; returns the allocated index.
  uint64_t append_raw_kl(gsl::string_view key, gsl::string_view type_name, gsl::string_view data);
  void set_raw_ttl(gsl::string_view key, clock::time_point expire_at);

  // History
  size_t call_count() const noexcept { return calls_.size(); }
  const db_request_record *call_at(size_t index) const;
  size_t calls(op_type op) const;
  size_t calls(gsl::string_view table_name) const;
  size_t calls(gsl::string_view table_name, op_type op) const;
  const db_request_record *last_call(gsl::string_view table_name) const;
  void clear_history() noexcept { calls_.clear(); }

  // Per-table mock rules with per-interface callbacks. table_name is the generated db namespace name
  // (e.g. "login_auth"); requests whose keys embed a different table, and operations without a matching
  // callback, fall through to the common mock layer (in-memory backend).
  db_table_rule_handle mock_table(gsl::string_view table_name);
  // Extract the table name from a generated-layout key; empty when not recognizable.
  std::string extract_table_name(gsl::string_view key) const;

  // Helpers for per-table callbacks: materialize a record into the shared-message wrapper using the
  // registered message factory (returns false when no factory is registered for the message type),
  // and build one key_list output entry the same way.
  bool make_output(
      rpc::context &ctx, const google::protobuf::Message &source,
      atfw::util::memory::strong_rc_ptr<rpc::shared_abstract_message<google::protobuf::Message>> &out) const;
  int32_t make_kl_entry(rpc::context &ctx, const google::protobuf::Message &source, uint64_t list_index,
                        db_key_list_message_result_t &out) const;

  std::string get_diagnostic() const { return diagnostic_; }

 private:
  friend class runtime;
  void bind();
  void unbind();

  bool handle(const rpc::db::hash_table::unit_test_request &req, int32_t &result_code);

  struct kv_record {
    std::string type_name;
    std::string data;
    uint64_t version = 0;
    bool has_expire = false;
    clock::time_point expire_at{};
  };

  struct kl_entry {
    uint64_t index = 0;
    std::string type_name;
    std::string data;
  };

  struct kl_record {
    std::deque<kl_entry> entries;
    uint64_t next_index = 1;
    bool has_expire = false;
    clock::time_point expire_at{};
  };

  kv_record *find_live_kv(gsl::string_view key);
  kl_record *find_live_kl(gsl::string_view key);
  bool is_expired(bool has_expire, clock::time_point expire_at) const;

  int32_t make_kv_output(const kv_record &record, rpc::context &ctx, db_key_value_message_result_t *output) const;
  // Production unpackers never set db_key_list_message_result_t::version (it stays 0), so the mock
  // reports no KL version either.
  int32_t make_kl_output(const kl_entry &entry, rpc::context &ctx, db_key_list_message_result_t &output) const;
  // Drop all fields not listed in req.partly_get_fields from req.kv_output (storage read path).
  int32_t filter_partly_fields(const rpc::db::hash_table::unit_test_request &req) const;
  // Apply the latest matching active per-table rule: invoke its callback for this op (or the table
  // catch-all). Returns true when a callback handled the operation (outcome already materialized into
  // req outputs and result_code); false means fall through to the in-memory backend.
  bool apply_table_handlers(const rpc::db::hash_table::unit_test_request &req, gsl::string_view table_name,
                            int32_t &result_code);

  int32_t on_kv_get_all(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kv_partly_get(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kv_set(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kv_inc_field(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kl_get_all(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kl_get_by_indexs(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kl_update_by_index(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kl_add_index(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_kl_remove_by_index(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_remove_all(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_set_ttl(const rpc::db::hash_table::unit_test_request &req);
  int32_t on_remove_ttl(const rpc::db::hash_table::unit_test_request &req);

  bool bound_ = false;
  bool clock_overridden_ = false;
  clock::time_point now_override_{};
  std::string diagnostic_;
  std::unordered_map<std::string, kv_record> kv_records_;
  std::unordered_map<std::string, kl_record> kl_records_;
  std::unordered_map<std::string, message_factory_t> factories_;
  std::vector<std::shared_ptr<detail::db_table_rule_state>> table_rules_;
  // Clear closures of typed per-interface handlers installed by generated code; invoked on unbind.
  std::vector<std::function<void()>> typed_handler_clearers_;
  std::deque<db_request_record> calls_;
};

}  // namespace testing
}  // namespace atframework
