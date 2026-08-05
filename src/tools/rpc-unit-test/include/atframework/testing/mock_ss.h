// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/extension/atframework.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp_config.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <atframework/testing/raw_transport.h>
#include <atframework/testing/runtime.h>

LIBATAPP_MACRO_NAMESPACE_BEGIN
class app;
LIBATAPP_MACRO_NAMESPACE_END

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace atframework {
namespace testing {

class mock_ss;

// Untyped view of one captured SS request, passed to mock handlers. The concrete request type is
// guaranteed by descriptor validation at registration time; downcast with static_cast.
struct ATFW_UTIL_SYMBOL_VISIBLE ss_request_view {
  const google::protobuf::Message &body;
  const atframework::SSMsgHead &head;
  uint64_t target_node_id = 0;
  std::string target_node_name;
};

// Behavior and matching options of one SS rule.
struct ATFW_UTIL_SYMBOL_VISIBLE ss_rule_options {
  // Only match messages sent to this node (0 = any).
  uint64_t match_node_id = 0;
  // How many times this rule can fire (0 = unlimited). FIFO scripts are built from several
  // times(1) rules in insertion order.
  uint32_t times = 0;
  // Extra pump generations to delay the response after the rule fired.
  uint32_t delay_generations = 0;
  // Never inject a response, even for rpc_request messages (simulates a lost response / timeout).
  bool no_response = false;
  // Inject a response with an empty rpc_response.type_url.
  bool malformed_type_url = false;
  // Inject a response with an unparsable body.
  bool malformed_body = false;
};

namespace detail {
struct ss_rule_state {
  std::string rpc_name;
  std::string request_type_url;
  std::string response_type_url;
  ss_rule_options options;
  // (request SSMsg, response SSMsg) -> error code. May be empty for record-only rules.
  std::function<int(const atframework::SSMsg &, atframework::SSMsg &, uint64_t, gsl::string_view)> invoker;
  // Explicit error response without calling any handler (0 = use invoker).
  int32_t forced_error_code = 0;
  uint32_t remaining_times = 0;
  bool active = true;
};

struct ss_expectation_state {
  const mock_ss *engine = nullptr;
  std::string rpc_name;
  int32_t expected_times = -1;
  uint64_t expected_node_id = 0;
};
}  // namespace detail

// RAII handle of one SS rule. The rule is disabled when the handle is destroyed.
class RPC_UNIT_TEST_API ss_rule_handle {
 public:
  ss_rule_handle() = default;
  ~ss_rule_handle();

  ss_rule_handle(const ss_rule_handle &) = delete;
  ss_rule_handle &operator=(const ss_rule_handle &) = delete;
  ss_rule_handle(ss_rule_handle &&other) noexcept;
  ss_rule_handle &operator=(ss_rule_handle &&other) noexcept;

  void reset();
  explicit operator bool() const noexcept { return !!rule_; }

 private:
  friend class mock_ss;
  explicit ss_rule_handle(std::shared_ptr<detail::ss_rule_state> rule);

  std::shared_ptr<detail::ss_rule_state> rule_;
};

// One recorded SS call.
struct ATFW_UTIL_SYMBOL_VISIBLE ss_call_record {
  std::string rpc_name;
  uint64_t target_node_id = 0;
  std::string target_node_name;
  uint64_t sequence = 0;
  bool is_stream = false;
  bool matched_rule = false;
  std::string request_type_url;
  atframework::SSMsgHead head;
};

// Expectation registered on the engine, verified by verify_expectations() (runtime::stop calls it).
// Copies share the same state; fluent setters mutate the shared state.
class RPC_UNIT_TEST_API ss_expectation {
 public:
  ss_expectation() = default;

  ss_expectation &times(int32_t expected) noexcept;
  ss_expectation &to_node(uint64_t node_id) noexcept;

  bool verify(std::string &diagnostic) const;

 private:
  friend class mock_ss;
  explicit ss_expectation(std::shared_ptr<detail::ss_expectation_state> state);

  std::shared_ptr<detail::ss_expectation_state> state_;
};

// Typed SS rule engine on top of raw_transport. Outbound SSMsg records are consumed from the transport
// after the one-generation barrier, user handlers run inside the pump (never in the send call stack),
// and responses are injected through the real app forward event into ss_msg_dispatcher.
class RPC_UNIT_TEST_API mock_ss {
 public:
  mock_ss();
  ~mock_ss();

  mock_ss(const mock_ss &) = delete;
  mock_ss &operator=(const mock_ss &) = delete;

  bool is_active() const noexcept;

  // Register a unary handler. The full RPC name ("<ServiceFullName>/<MethodName>") and the
  // request/response type names are validated against the protobuf generated pool; invalid
  // registrations fail immediately (check the returned handle and get_diagnostic()). The handler
  // receives the parsed request through ss_request_view and fills a fresh response instance of the
  // registered response type; descriptor lookup, prototype creation and parse/serialize all happen
  // inside the library (see 3.6 in IMPLEMENTATION_PLAN.md: no template API surface).
  ss_rule_handle mock(gsl::string_view full_rpc_name, gsl::string_view request_type_name,
                      gsl::string_view response_type_name,
                      std::function<int(const ss_request_view &, google::protobuf::Message &)> handler,
                      const ss_rule_options &options = ss_rule_options{});

  // Register an untyped handler working directly on SSMsg (advanced use, no descriptor validation of
  // the body types).
  ss_rule_handle mock_untyped(gsl::string_view full_rpc_name,
                              std::function<int(const atframework::SSMsg &, atframework::SSMsg &)> handler,
                              const ss_rule_options &options = ss_rule_options{});

  // Register an immediate error response for a (normally unmatched) RPC.
  ss_rule_handle mock_error(gsl::string_view full_rpc_name, int32_t error_code,
                            const ss_rule_options &options = ss_rule_options{});

  // History
  size_t calls() const noexcept { return history_.size(); }
  size_t calls(gsl::string_view full_rpc_name) const;
  const ss_call_record *call_at(size_t index) const noexcept;

  // Expectations (verified at verify_expectations(), which runtime::stop() calls before teardown).
  ss_expectation expect(gsl::string_view full_rpc_name);
  bool verify_expectations(std::string &diagnostic) const;

  // Unmatched rpc_request messages get an immediate error response (fast fail, per the default
  // strict policy); unmatched stream/notification messages are only recorded. Set drop_unmatched_request
  // to just record unmatched rpc_request messages instead.
  void set_drop_unmatched_request(bool drop) noexcept { drop_unmatched_request_ = drop; }

  const std::string &get_diagnostic() const noexcept { return diagnostic_; }

  // Number of rules that were registered but never fired (only times-limited rules are counted).
  size_t unconsumed_rule_count() const noexcept;

 private:
  friend class runtime;
  friend class ss_expectation;

  void bind(atfw::atapp::app *owner, raw_transport &transport);
  void unbind();
  void deliver_pending();

  ss_rule_handle mock_typed(
      gsl::string_view full_rpc_name, gsl::string_view request_type_url, gsl::string_view response_type_url,
      std::function<int(const atframework::SSMsg &, atframework::SSMsg &, uint64_t, gsl::string_view)> invoker,
      const ss_rule_options &options);

  void inject_response(const atframework::SSMsg &request_msg, atframework::SSMsg &response_msg, int32_t error_code,
                       uint64_t target_node_id, gsl::string_view target_node_name, const ss_rule_options *options);

  struct pending_response {
    uint64_t deliver_at_generation = 0;
    uint64_t target_node_id = 0;
    std::vector<unsigned char> payload;
  };

  atfw::atapp::app *owner_ = nullptr;
  raw_transport *transport_ = nullptr;
  raw_transport::outbound_cursor cursor_;
  std::vector<std::shared_ptr<detail::ss_rule_state>> rules_;
  std::deque<ss_call_record> history_;
  std::vector<std::shared_ptr<detail::ss_expectation_state>> expectations_;
  std::deque<pending_response> pending_responses_;
  bool drop_unmatched_request_ = false;
  std::string diagnostic_;
};

}  // namespace testing
}  // namespace atframework
