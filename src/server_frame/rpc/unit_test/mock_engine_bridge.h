// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/extension/atframework.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace rpc {
namespace unit_test {

// Server-frame-owned mirror of the tool-side SS rule options, so generated mock code never
// references rpc-unit-test library types (see 3.6 in IMPLEMENTATION_PLAN.md).
struct ATFW_UTIL_SYMBOL_VISIBLE ss_mock_rule_options {
  uint64_t match_node_id = 0;
  uint32_t times = 0;
  uint32_t delay_generations = 0;
  bool no_response = false;
  bool malformed_type_url = false;
  bool malformed_body = false;
};

// Server-frame-owned view of one captured SS request, converted by the tool-side adapter.
struct ATFW_UTIL_SYMBOL_VISIBLE ss_mock_request_view {
  const google::protobuf::Message *body = nullptr;
  const atframework::SSMsgHead *head = nullptr;
  uint64_t target_node_id = 0;
  std::string target_node_name;
};

using ss_mock_handler_t = std::function<int(const ss_mock_request_view &, google::protobuf::Message &)>;

// Type-erased invocation slots of the rpc-unit-test mock engines, registered by the test runtime
// (mock_ss::bind/mock_db::bind) and cleared on unbind. Generated mock implementations call through
// these slots so server_frame (and service binaries) never link the rpc-unit-test library. All
// slots are empty by default; generated helpers degrade to no-op/false when no engine is bound.
struct ATFW_UTIL_SYMBOL_VISIBLE mock_engine_bridge_t {
  // SS: register one unary rule, returns the deactivation token (reset to disable the rule).
  std::function<std::shared_ptr<void>(gsl::string_view full_rpc_name, gsl::string_view request_type_name,
                                      gsl::string_view response_type_name, ss_mock_handler_t handler,
                                      const ss_mock_rule_options &options)>
      register_ss_rule;

  // DB: per-table rules (op is rpc::db::hash_table::unit_test_request::op_type as int32_t).
  std::function<std::shared_ptr<void>(gsl::string_view table_name, int32_t op, int32_t error_code)>
      db_set_error_rule;
  std::function<std::shared_ptr<void>(gsl::string_view table_name)> db_force_not_found_rule;

  // DB: raw entry access (direct backend read/write, no RPC, no task context).
  std::function<void(gsl::string_view key, gsl::string_view type_name, gsl::string_view data, uint64_t version)>
      db_set_raw_kv;
  std::function<bool(gsl::string_view key, std::string *type_name, std::string *data, uint64_t *version)>
      db_get_raw_kv;
  std::function<uint64_t(gsl::string_view key, gsl::string_view type_name, gsl::string_view data)>
      db_append_raw_kl;
  std::function<bool(gsl::string_view key, std::vector<std::tuple<uint64_t, std::string, std::string>> *entries)>
      db_get_raw_kl;
  std::function<void(gsl::string_view key, uint64_t ttl_seconds)> db_set_raw_ttl;
};

// RAII handle of one rule registered through the bridge. Destroying the handle deactivates the rule.
class SERVER_FRAME_API mock_rule_handle {
 public:
  mock_rule_handle() = default;
  explicit mock_rule_handle(std::shared_ptr<void> token) noexcept : token_(std::move(token)) {}

  void reset() noexcept { token_.reset(); }
  explicit operator bool() const noexcept { return !!token_; }

 private:
  std::shared_ptr<void> token_;
};

// Merge-registered by the tool engines: non-null slots of `slots` overwrite the current ones, so
// mock_ss::bind only replaces the SS slot and mock_db::bind only the DB slots. Not thread-safe by
// design: unit tests drive registration and RPC from the same thread context as the runtime pump.
SERVER_FRAME_API void merge_mock_engine_bridge_for_unit_test(mock_engine_bridge_t slots);
SERVER_FRAME_API void clear_ss_mock_engine_bridge_slot();
SERVER_FRAME_API void clear_db_mock_engine_bridge_slots();
SERVER_FRAME_API const mock_engine_bridge_t &get_mock_engine_bridge_for_unit_test() noexcept;

}  // namespace unit_test
}  // namespace rpc

#endif
