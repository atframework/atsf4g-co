// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <config/compiler/protobuf_prefix.h>
#include <google/protobuf/message.h>
#include <config/compiler/protobuf_suffix.h>

#include <atframework/testing/runtime.h>

#include "rpc/db/hash_table.h"
#include "rpc/rpc_shared_message.h"

namespace atsf4g {
namespace testing {

class mock_db;

// One recorded hash_table operation.
struct ATFW_UTIL_SYMBOL_VISIBLE db_request_record {
  rpc::db::hash_table::unit_test_request::op_type op = rpc::db::hash_table::unit_test_request::op_type::kv_get_all;
  std::string key;
  int32_t result_code = 0;
};

// In-memory mock of the rpc::db::hash_table backend. Installed through the server_frame unit-test
// hook so every primitive operation (and therefore batch operations and generated db helpers such
// as rpc::db::uuid_allocator) is served synchronously without a real Redis or DB dispatcher init.
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
  using message_factory_t =
      std::function<rpc::shared_abstract_message<google::protobuf::Message>(rpc::context &)>;
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

  // History
  size_t call_count() const noexcept { return calls_.size(); }
  const db_request_record *call_at(size_t index) const;
  size_t calls(op_type op) const;
  void clear_history() noexcept { calls_.clear(); }

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

  int32_t make_kv_output(const kv_record &record, rpc::context &ctx,
                         db_key_value_message_result_t *output) const;
  // Production unpackers never set db_key_list_message_result_t::version (it stays 0), so the mock
  // reports no KL version either.
  int32_t make_kl_output(const kl_entry &entry, rpc::context &ctx, db_key_list_message_result_t &output) const;

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
  std::deque<db_request_record> calls_;
};

}  // namespace testing
}  // namespace atsf4g
