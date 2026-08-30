// Copyright 2026 atframework

#include <atframework/testing/mock_ss.h>

#include <atframe/atapp.h>
#include <atframe/modules/service_discovery_module.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

#include <config/logic_config.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/unit_test/mock_engine_bridge.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "detail/pending_drain.h"

namespace atframework {
namespace testing {

ss_rule_handle::ss_rule_handle(std::shared_ptr<detail::ss_rule_state> rule) : rule_(std::move(rule)) {}

ss_rule_handle::~ss_rule_handle() { reset(); }

ss_rule_handle::ss_rule_handle(ss_rule_handle &&other) noexcept : rule_(std::move(other.rule_)) {}

ss_rule_handle &ss_rule_handle::operator=(ss_rule_handle &&other) noexcept {
  reset();
  rule_ = std::move(other.rule_);
  return *this;
}

void ss_rule_handle::reset() {
  if (rule_) {
    rule_->active = false;
    rule_.reset();
  }
}

ss_expectation::ss_expectation(std::shared_ptr<detail::ss_expectation_state> state) : state_(std::move(state)) {}

ss_expectation &ss_expectation::times(int32_t expected) noexcept {
  if (state_) {
    state_->expected_times = expected;
  }
  return *this;
}

ss_expectation &ss_expectation::to_node(uint64_t node_id) noexcept {
  if (state_) {
    state_->expected_node_id = node_id;
  }
  return *this;
}

bool ss_expectation::verify(std::string &diagnostic) const {
  if (!state_ || nullptr == state_->engine || !state_->engine->is_active()) {
    return true;
  }

  int32_t matched = 0;
  for (const auto &record : state_->engine->history_) {
    if (record.rpc_name != state_->rpc_name) {
      continue;
    }
    if (0 != state_->expected_node_id && record.target_node_id != state_->expected_node_id) {
      continue;
    }
    ++matched;
  }

  if (state_->expected_times >= 0 && matched != state_->expected_times) {
    diagnostic = "ss rpc " + state_->rpc_name + " expected " + std::to_string(state_->expected_times) +
                 " call(s), got " + std::to_string(matched);
    return false;
  }
  return true;
}

mock_ss::mock_ss() = default;

mock_ss::~mock_ss() { unbind(); }

bool mock_ss::is_active() const noexcept { return nullptr != owner_; }

ss_rule_handle mock_ss::mock_untyped(
    gsl::string_view full_rpc_name,
    std::function<rpc::result_code_type(rpc::context &, const atfw::SSMsg &, atfw::SSMsg &)> handler,
    const ss_rule_options &options) {
  auto invoker = [handler = std::move(handler)](rpc::context &ctx, const atfw::SSMsg &request_msg,
                                                atfw::SSMsg &response_msg, uint64_t,
                                                gsl::string_view) -> rpc::result_code_type {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handler(ctx, request_msg, response_msg)));
  };
  return mock_typed(full_rpc_name, "", "", std::move(invoker), options);
}

ss_rule_handle mock_ss::mock(
    gsl::string_view full_rpc_name, gsl::string_view request_type_name, gsl::string_view response_type_name,
    std::function<rpc::result_code_type(const ss_request_view &, google::protobuf::Message &)> handler,
    const ss_rule_options &options) {
  if (!handler) {
    diagnostic_ = "empty handler for rpc: " + std::string{full_rpc_name.data(), full_rpc_name.size()};
    return ss_rule_handle{};
  }
  std::string request_name{request_type_name.data(), request_type_name.size()};
  std::string response_name{response_type_name.data(), response_type_name.size()};
  const auto *request_desc = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(request_name);
  if (nullptr == request_desc) {
    diagnostic_ = "request type not found in generated pool: " + request_name;
    return ss_rule_handle{};
  }
  const auto *response_desc = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(response_name);
  if (nullptr == response_desc) {
    diagnostic_ = "response type not found in generated pool: " + response_name;
    return ss_rule_handle{};
  }

  auto invoker = [handler = std::move(handler), request_desc, response_desc](
                     rpc::context &ctx, const atfw::SSMsg &request_msg, atfw::SSMsg &response_msg,
                     uint64_t target_node_id, gsl::string_view target_node_name) -> rpc::result_code_type {
    const auto *request_proto = google::protobuf::MessageFactory::generated_factory()->GetPrototype(request_desc);
    const auto *response_proto = google::protobuf::MessageFactory::generated_factory()->GetPrototype(response_desc);
    if (nullptr == request_proto || nullptr == response_proto) {
      RPC_RETURN_CODE(-1);
    }
    std::unique_ptr<google::protobuf::Message> request_body{request_proto->New()};
    std::unique_ptr<google::protobuf::Message> response_body{response_proto->New()};
    if (nullptr == request_body || nullptr == response_body) {
      RPC_RETURN_CODE(-1);
    }
    if (!request_body->ParseFromString(request_msg.body_bin())) {
      RPC_RETURN_CODE(-1);
    }
    ss_request_view request_view{*request_body, request_msg.head(), target_node_id,
                                 std::string{target_node_name.data(), target_node_name.size()}, &ctx};
    int res = RPC_AWAIT_CODE_RESULT(handler(request_view, *response_body));
    if (0 == res) {
      response_msg.set_body_bin(response_body->SerializeAsString());
    }
    RPC_RETURN_CODE(res);
  };
  return mock_typed(full_rpc_name, request_type_name, response_type_name, std::move(invoker), options);
}

ss_rule_handle mock_ss::mock_error(gsl::string_view full_rpc_name, int32_t error_code, const ss_rule_options &options) {
  ss_rule_handle ret = mock_typed(full_rpc_name, "", "", nullptr, options);
  if (ret.rule_) {
    ret.rule_->forced_error_code = error_code;
  }
  return ret;
}

size_t mock_ss::calls(gsl::string_view full_rpc_name) const {
  size_t ret = 0;
  for (const auto &record : history_) {
    if (record.rpc_name.size() == full_rpc_name.size() &&
        0 == record.rpc_name.compare(0, std::string::npos, full_rpc_name.data(), full_rpc_name.size())) {
      ++ret;
    }
  }
  return ret;
}

const ss_call_record *mock_ss::call_at(size_t index) const noexcept {
  if (index >= history_.size()) {
    return nullptr;
  }
  return &history_[index];
}

ss_expectation mock_ss::expect(gsl::string_view full_rpc_name) {
  auto state = std::make_shared<detail::ss_expectation_state>();
  state->engine = this;
  state->rpc_name = std::string{full_rpc_name.data(), full_rpc_name.size()};
  expectations_.push_back(state);
  return ss_expectation{std::move(state)};
}

bool mock_ss::verify_expectations(std::string &diagnostic) const {
  bool ret = true;
  for (const auto &state : expectations_) {
    if (!state) {
      continue;
    }
    ss_expectation expectation{state};
    std::string one_diagnostic;
    if (!expectation.verify(one_diagnostic)) {
      if (!diagnostic.empty()) {
        diagnostic += "; ";
      }
      diagnostic += one_diagnostic;
      ret = false;
    }
  }
  return ret;
}

size_t mock_ss::unconsumed_rule_count() const noexcept {
  size_t ret = 0;
  for (const auto &rule : rules_) {
    if (rule && rule->active && rule->options.times > 0 && rule->remaining_times > 0) {
      ++ret;
    }
  }
  return ret;
}

void mock_ss::bind(atfw::atapp::app *owner, raw_transport &transport) {
  owner_ = owner;
  transport_ = &transport;
  cursor_ = raw_transport::outbound_cursor{};
  lifecycle_token_ = std::make_shared<char>(0);
  diagnostic_.clear();

  // Expose this engine to generated mock helpers through the server-frame bridge, so generated
  // code never links the rpc-unit-test library.
  rpc::unit_test::mock_engine_bridge_t slots;
  slots.register_ss_rule = [this](gsl::string_view full_rpc_name, gsl::string_view request_type_name,
                                  gsl::string_view response_type_name, rpc::unit_test::ss_mock_handler_t handler,
                                  const rpc::unit_test::ss_mock_rule_options &options) -> std::shared_ptr<void> {
    ss_rule_options engine_options;
    engine_options.match_node_id = options.match_node_id;
    engine_options.times = options.times;
    engine_options.delay_generations = options.delay_generations;
    engine_options.no_response = options.no_response;
    engine_options.malformed_type_url = options.malformed_type_url;
    engine_options.malformed_body = options.malformed_body;
    ss_rule_handle rule = mock(
        full_rpc_name, request_type_name, response_type_name,
        [handler = std::move(handler)](const ss_request_view &view,
                                       google::protobuf::Message &response) -> rpc::result_code_type {
          rpc::unit_test::ss_mock_request_view bridge_view{&view.body, &view.head, view.target_node_id,
                                                           view.target_node_name, view.context};
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handler(bridge_view, response)));
        },
        engine_options);
    if (!rule.rule_) {
      return nullptr;
    }
    // Detach the rule state before the local handle destroys it; deactivation is owned by the
    // returned bridge token from now on.
    std::shared_ptr<detail::ss_rule_state> state = rule.rule_;
    rule.rule_.reset();
    return std::shared_ptr<void>{state.get(), [state](void *) { state->active = false; }};
  };
  rpc::unit_test::merge_mock_engine_bridge_for_unit_test(std::move(slots));
}

void mock_ss::unbind() {
  rpc::unit_test::clear_ss_mock_engine_bridge_slot();
  // Expire the lifecycle token so handler tasks still running after teardown skip their response and
  // never touch this engine again.
  lifecycle_token_.reset();
  for (auto &rule : rules_) {
    if (rule) {
      rule->active = false;
    }
  }
  rules_.clear();
  for (auto &state : expectations_) {
    if (state) {
      state->engine = nullptr;
    }
  }
  expectations_.clear();
  history_.clear();
  pending_responses_.clear();
  drop_unmatched_request_ = false;
  owner_ = nullptr;
  transport_ = nullptr;
}

void mock_ss::deliver_pending() {
  if (!is_active() || nullptr == transport_) {
    return;
  }

  // Consume transport records that crossed the one-generation barrier.
  std::vector<const outbound_message *> records;
  transport_->collect_outbound(cursor_, records);
  for (const outbound_message *record : records) {
    if (nullptr == record || record->type != static_cast<int32_t>(::atfw::component::message_type::kInServerMessage)) {
      continue;
    }

    // Skip messages that the transport layer already marked as errored (immediate_error != 0): the send
    // was rejected and no mock response should be generated for it.
    if (record->immediate_error != 0) {
      continue;
    }

    atfw::SSMsg request_msg;
    if (!request_msg.ParseFromArray(record->payload.data(), static_cast<int>(record->payload.size()))) {
      FWLOGERROR("mock_ss failed to parse outbound SSMsg to {:#x}({}), {} bytes", record->target_node_id,
                 record->target_node_name, record->payload.size());
      continue;
    }

    ss_call_record history_record;
    history_record.target_node_id = record->target_node_id;
    history_record.target_node_name = record->target_node_name;
    history_record.sequence = request_msg.head().sequence();
    history_record.is_stream = request_msg.head().has_rpc_stream();
    history_record.head.CopyFrom(request_msg.head());
    if (request_msg.head().has_rpc_request()) {
      history_record.rpc_name = request_msg.head().rpc_request().rpc_name();
      history_record.request_type_url = request_msg.head().rpc_request().type_url();
    } else if (request_msg.head().has_rpc_stream()) {
      history_record.rpc_name = request_msg.head().rpc_stream().rpc_name();
      history_record.request_type_url = request_msg.head().rpc_stream().type_url();
    } else if (request_msg.head().has_rpc_response()) {
      history_record.rpc_name = request_msg.head().rpc_response().rpc_name();
      history_record.request_type_url = request_msg.head().rpc_response().type_url();
    }

    // Find a matching rule in insertion order.
    std::shared_ptr<detail::ss_rule_state> matched_rule;
    for (auto &rule : rules_) {
      if (!rule || !rule->active) {
        continue;
      }
      if (rule->options.times > 0 && 0 == rule->remaining_times) {
        continue;
      }
      if (rule->rpc_name != history_record.rpc_name) {
        continue;
      }
      if (0 != rule->options.match_node_id && rule->options.match_node_id != record->target_node_id) {
        continue;
      }
      matched_rule = rule;
      break;
    }
    history_record.matched_rule = !!matched_rule;
    const std::string rpc_name = history_record.rpc_name;
    history_.push_back(std::move(history_record));

    const bool needs_response = request_msg.head().has_rpc_request();

    if (!matched_rule) {
      // Default behaviors: notifications are recorded and dropped; rpc_request calls fail fast.
      if (needs_response && !drop_unmatched_request_) {
        FWLOGERROR(
            "mock_ss has no rule for rpc {} to {:#x}({}), sequence {}. Register a mock rule or set "
            "drop_unmatched_request. Available rules:",
            rpc_name, record->target_node_id, record->target_node_name, request_msg.head().sequence());
        for (const auto &rule : rules_) {
          if (rule && rule->active) {
            FWLOGERROR("  rule: {} (node: {:#x})", rule->rpc_name, rule->options.match_node_id);
          }
        }
        atfw::SSMsg response_msg;
        inject_response(request_msg, response_msg, PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, record->target_node_id,
                        record->target_node_name, nullptr);
      }
      continue;
    }

    // Consume the rule budget only after we have committed to handling it (forced-error, sync invoker,
    // or successful async-start). A failed async-start below refunds the budget.
    if (matched_rule->options.times > 0 && matched_rule->remaining_times > 0) {
      --matched_rule->remaining_times;
    }

    if (matched_rule->options.no_response) {
      continue;
    }

    const int32_t forced_error_code = matched_rule->forced_error_code;
    if (0 != forced_error_code || !matched_rule->invoker) {
      if (needs_response) {
        atfw::SSMsg response_msg;
        inject_response(request_msg, response_msg, forced_error_code, record->target_node_id, record->target_node_name,
                        &matched_rule->options);
      }
      continue;
    }

    // The handler is a coroutine (rpc::result_code_type): drive it as a task so it may await nested
    // RPC calls; the response is injected when it completes (delay generations count from completion).
    std::weak_ptr<char> lifecycle = lifecycle_token_;
    auto invoke_result = rpc::async_invoke(
        "atfw::testing::mock_ss", "mock_ss:invoke",
        [this, lifecycle = std::move(lifecycle), matched_rule, request_msg, needs_response,
         target_node_id = record->target_node_id,
         target_node_name = record->target_node_name](rpc::context &ctx) mutable -> rpc::result_code_type {
          atfw::SSMsg response_msg;
          int32_t res = RPC_AWAIT_CODE_RESULT(
              matched_rule->invoker(ctx, request_msg, response_msg, target_node_id, target_node_name));
          if (needs_response && !lifecycle.expired() && is_active()) {
            inject_response(request_msg, response_msg, res, target_node_id, target_node_name, &matched_rule->options);
          }
          RPC_RETURN_CODE(res);
        },
        std::chrono::seconds{30});
    if (!invoke_result.is_success()) {
      // Refund the budget consumed above — the handler never ran.
      if (matched_rule->options.times > 0 && matched_rule->remaining_times > 0) {
        ++matched_rule->remaining_times;
      }
      if (needs_response) {
        FWLOGERROR("mock_ss failed to start the handler task for rpc {}, fail with an immediate error response",
                   rpc_name);
        atfw::SSMsg response_msg;
        inject_response(request_msg, response_msg, PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY,
                        record->target_node_id, record->target_node_name, &matched_rule->options);
      }
    }
  }

  // Deliver due responses. See detail::drain_due_events for the due-order scan and the
  // re-entrancy-safe two-phase contract.
  std::vector<pending_response> due_events =
      detail::drain_due_events(pending_responses_, transport_->get_current_generation());

  for (auto &event : due_events) {
    auto discovery_module = owner_->get_service_discovery_module();
    if (!discovery_module) {
      continue;
    }
    auto sender_node = discovery_module->get_global_discovery().get_node_by_id(event.target_node_id);
    if (!sender_node) {
      FWLOGERROR("mock_ss drops a response to unknown node {:#x}", event.target_node_id);
      continue;
    }

    atfw::atapp::app::message_t msg;
    msg.type = static_cast<int32_t>(::atfw::component::message_type::kInServerMessage);
    msg.message_sequence = 0;
    msg.data = gsl::span<const unsigned char>{event.payload.data(), event.payload.size()};
    msg.metadata = nullptr;

    atfw::atapp::app::message_sender_t sender;
    sender.direct_source_id = sender_node->get_discovery_info().id();
    sender.id = sender_node->get_discovery_info().id();
    sender.name = sender_node->get_discovery_info().name();
    sender.remote = owner_->get_endpoint(sender.id);

    owner_->trigger_event_on_forward_request(sender, msg);
  }
}

ss_rule_handle mock_ss::mock_typed(
    gsl::string_view full_rpc_name, gsl::string_view request_type_url, gsl::string_view response_type_url,
    std::function<rpc::result_code_type(rpc::context &, const atfw::SSMsg &, atfw::SSMsg &, uint64_t, gsl::string_view)>
        invoker,
    const ss_rule_options &options) {
  // Validate "<ServiceFullName>/<MethodName>" against the generated pool.
  size_t separator = full_rpc_name.find_last_of('/');
  if (gsl::string_view::npos == separator || 0 == separator || separator + 1 >= full_rpc_name.size()) {
    diagnostic_ = "invalid rpc full name: " + std::string{full_rpc_name.data(), full_rpc_name.size()};
    return ss_rule_handle{};
  }
  std::string service_name{full_rpc_name.data(), separator};
  std::string method_name{full_rpc_name.data() + separator + 1, full_rpc_name.size() - separator - 1};

  const auto *service = google::protobuf::DescriptorPool::generated_pool()->FindServiceByName(service_name);
  if (nullptr == service) {
    diagnostic_ = "service not found in generated pool: " + service_name;
    return ss_rule_handle{};
  }
  const auto *method = service->FindMethodByName(method_name);
  if (nullptr == method) {
    diagnostic_ = "method not found in service " + service_name + ": " + method_name;
    return ss_rule_handle{};
  }
  if (!request_type_url.empty() && method->input_type()->full_name() != request_type_url) {
    diagnostic_ = "request type mismatch for " + std::string(full_rpc_name.data(), full_rpc_name.size()) + ": expect " +
                  std::string(method->input_type()->full_name()) + ", got " +
                  std::string(request_type_url.data(), request_type_url.size());
    return ss_rule_handle{};
  }
  if (!response_type_url.empty() && method->output_type()->full_name() != response_type_url) {
    diagnostic_ = "response type mismatch for " + std::string(full_rpc_name.data(), full_rpc_name.size()) +
                  ": expect " + std::string(method->output_type()->full_name()) + ", got " +
                  std::string(response_type_url.data(), response_type_url.size());
    return ss_rule_handle{};
  }

  auto rule = std::make_shared<detail::ss_rule_state>();
  rule->rpc_name = std::string(full_rpc_name.data(), full_rpc_name.size());
  rule->request_type_url = std::string(request_type_url.data(), request_type_url.size());
  rule->response_type_url = std::string(response_type_url.data(), response_type_url.size());
  if (rule->response_type_url.empty()) {
    rule->response_type_url = std::string(method->output_type()->full_name());
  }
  rule->options = options;
  rule->invoker = std::move(invoker);
  rule->remaining_times = options.times;
  if (rule->options.preempt) {
    // Preempt rules match before every non-preempt rule and keep FIFO among themselves, so a
    // one-shot fault rule shadows a long-lived default rule exactly until its budget is consumed.
    auto insert_at = rules_.begin();
    while (insert_at != rules_.end() && *insert_at && (*insert_at)->options.preempt) {
      ++insert_at;
    }
    rules_.insert(insert_at, rule);
  } else {
    rules_.push_back(rule);
  }
  return ss_rule_handle{std::move(rule)};
}

void mock_ss::inject_response(const atfw::SSMsg &request_msg, atfw::SSMsg &response_msg, int32_t error_code,
                              uint64_t target_node_id, gsl::string_view target_node_name,
                              const ss_rule_options *options) {
  if (!is_active() || nullptr == transport_) {
    return;
  }

  atfw::SSMsgHead *head = response_msg.mutable_head();
  if (nullptr == head) {
    return;
  }

  head->set_error_code(error_code);
  head->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                          atfw::util::time::time_utility::sys_now().time_since_epoch())
                          .count());
  head->set_sequence(request_msg.head().sequence());
  // The response comes from the remote node the request was sent to.
  head->set_node_id(target_node_id);
  head->set_node_name(target_node_name.data(), target_node_name.size());
  head->set_source_task_id(0);
  head->set_destination_task_id(request_msg.head().source_task_id());

  auto *rpc_response = head->mutable_rpc_response();
  if (nullptr != rpc_response) {
    rpc_response->set_version(logic_config::me()->get_atframework_settings().rpc_version());
    if (request_msg.head().has_rpc_request()) {
      rpc_response->set_rpc_name(request_msg.head().rpc_request().rpc_name());
      rpc_response->mutable_caller_timestamp()->CopyFrom(request_msg.head().rpc_request().caller_timestamp());
    }
    rpc_response->set_caller_node_id(logic_config::me()->get_local_server_id());
    rpc_response->set_caller_node_name(logic_config::me()->get_local_server_name());
  }

  // Derive the response type url from the service descriptor.
  std::string response_type_url;
  if (request_msg.head().has_rpc_request()) {
    const std::string &rpc_name = request_msg.head().rpc_request().rpc_name();
    size_t separator = rpc_name.find_last_of('/');
    if (std::string::npos != separator) {
      const auto *service =
          google::protobuf::DescriptorPool::generated_pool()->FindServiceByName(rpc_name.substr(0, separator));
      if (nullptr != service) {
        const auto *method = service->FindMethodByName(rpc_name.substr(separator + 1));
        if (nullptr != method) {
          response_type_url = method->output_type()->full_name();
        }
      }
    }
  }
  if (nullptr != rpc_response && (nullptr == options || !options->malformed_type_url)) {
    rpc_response->set_type_url(response_type_url);
  }

  if (nullptr != options && options->malformed_body) {
    response_msg.set_body_bin("\xde\xad\xbe\xef", 4);
  }

  pending_response event;
  event.deliver_at_generation =
      transport_->get_current_generation() + (nullptr == options ? 0 : options->delay_generations);
  event.target_node_id = target_node_id;
  std::string serialized = response_msg.SerializeAsString();
  event.payload.assign(serialized.begin(), serialized.end());
  pending_responses_.push_back(std::move(event));
}

}  // namespace testing
}  // namespace atframework
