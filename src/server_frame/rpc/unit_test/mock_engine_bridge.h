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
#include <utility>

#include "rpc/rpc_common_types.h"

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace rpc {
class context;

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

// Server-frame-owned view of one captured SS request, converted by the tool-side adapter. context is
// the coroutine context of the task driving the handler (use it to await nested RPC calls).
struct ATFW_UTIL_SYMBOL_VISIBLE ss_mock_request_view {
  const google::protobuf::Message *body = nullptr;
  const atframework::SSMsgHead *head = nullptr;
  uint64_t target_node_id = 0;
  std::string target_node_name;
  rpc::context *context = nullptr;
};

// SS/DB mock handlers return rpc::result_code_type so a handler may itself be a coroutine and await
// nested RPC calls (e.g. a mock SS handler calling another service, or a mock DB handler composing
// several table ops). Plain synchronous handlers simply use RPC_RETURN_CODE(...) without co_await.
using ss_mock_handler_t =
    std::function<rpc::result_code_type(const ss_mock_request_view &, google::protobuf::Message &)>;

// Extensible in/out meta of one DB mock handler invocation (avoids bare scalar parameters so new
// fields can be added without changing handler signatures). version carries the record CAS version:
// in = expected version for write ops (0 = no CAS check), out = current record version for read ops
// and the new version for successful write ops.
struct ATFW_UTIL_SYMBOL_VISIBLE db_mock_meta {
  uint64_t version = 0;
};

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

  // DB: register one typed per-interface table handler. Generated code installs the handler into its
  // own TU-local slot and passes a clear closure here; the engine invokes all pending clear closures on
  // unbind so no typed handler survives a runtime teardown (clearing is idempotent; the RAII
  // mock_rule_handle clears the same slot on destruction).
  std::function<void(gsl::string_view table_name, int32_t op, std::function<void()> clear_handler)>
      db_register_typed_handler;
};

// RAII handle of one rule registered through the bridge. Destroying the handle deactivates the rule.
class ATFW_UTIL_SYMBOL_VISIBLE mock_rule_handle {
 public:
  ATFW_UTIL_FORCEINLINE mock_rule_handle() = default;
  ATFW_UTIL_FORCEINLINE explicit mock_rule_handle(std::shared_ptr<void> token) noexcept : token_(std::move(token)) {}

  ATFW_UTIL_FORCEINLINE mock_rule_handle(const mock_rule_handle&) = default;
  ATFW_UTIL_FORCEINLINE mock_rule_handle(mock_rule_handle&&) = default;
  ATFW_UTIL_FORCEINLINE mock_rule_handle& operator=(const mock_rule_handle&) = default;
  ATFW_UTIL_FORCEINLINE mock_rule_handle& operator=(mock_rule_handle&&) = default;

  ATFW_UTIL_FORCEINLINE void reset() noexcept { token_.reset(); }
  ATFW_UTIL_FORCEINLINE explicit operator bool() const noexcept { return !!token_; }

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
