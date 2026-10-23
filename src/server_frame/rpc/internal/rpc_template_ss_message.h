// Copyright 2026 atframework
// Created by owent on 2016/08/02.
//

#pragma once

#include <nostd/string_view.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <atframe/atapp_conf.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/etcdcli/etcd_discovery.h>

#include <config/server_frame_build_feature.h>

#include <config/logic_config.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <router/router_manager_base.h>
#include <router/router_manager_set.h>
#include <router/router_object_base.h>

#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <type_traits>
#include <utility>

#include "rpc/internal/rpc_template.h"  // IWYU pragma: keep
#include "rpc/rpc_utils.h"              // IWYU pragma: keep

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
#  include "rpc/unit_test/mock_engine_bridge.h"
#endif

namespace rpc {
namespace internal {
ATFW_UTIL_FORCEINLINE bool is_invalid_server_node(const atfw::atapp::etcd_discovery_node& destination_server) {
  return destination_server.get_discovery_info().id() == 0 || destination_server.get_discovery_info().name().empty();
}

ATFW_UTIL_FORCEINLINE bool is_invalid_server_node(uint64_t destination_server) { return destination_server == 0; }

SERVER_FRAME_API int setup_ss_rpc_request_header(atframework::SSMsgHead& head, task_type_trait::id_type task_id,
                                                 atfw::util::nostd::string_view service_full_name,
                                                 atfw::util::nostd::string_view rpc_full_name,
                                                 atfw::util::nostd::string_view type_full_name);

// Generated code supplies only method metadata and the identifiers specific to this call.
struct ATFW_UTIL_SYMBOL_VISIBLE ss_rpc_options {
  atfw::util::nostd::string_view service_full_name;
  atfw::util::nostd::string_view rpc_full_name;
  atfw::util::nostd::string_view response_type_full_name;
  const google::protobuf::MethodDescriptor* method = nullptr;
  gsl::span<const int> warning_codes;
  gsl::span<const int> info_codes;
  bool user_rpc = false;
  uint32_t zone_id = 0;
  uint64_t user_id = 0;

  ATFW_UTIL_FORCEINLINE ss_rpc_options() {}
};

template <class TServer>
struct ATFW_UTIL_SYMBOL_VISIBLE ss_rpc_unicast_target {
  const typename std::remove_reference<TServer>::type* server;
  bool ignore_discovery;

  ATFW_UTIL_FORCEINLINE ss_rpc_unicast_target(const typename std::remove_reference<TServer>::type* input_server,
                                              bool input_ignore_discovery) noexcept
      : server(input_server), ignore_discovery(input_ignore_discovery) {}

  ATFW_UTIL_FORCEINLINE bool valid() const { return nullptr != server && !is_invalid_server_node(*server); }

  ATFW_UTIL_FORCEINLINE always_ready_code_type send(context&, atframework::SSMsg& message, uint64_t& sequence) const {
    auto result = ss_msg_dispatcher::me()->send_to_proc(*server, message, ignore_discovery);
    sequence = message.head().sequence();
    return {static_cast<always_ready_code_type::value_type>(result)};
  }
};

struct ATFW_UTIL_SYMBOL_VISIBLE ss_rpc_broadcast_target {
  const ss_msg_logic_index* index;
  atfw::atapp::protocol::atapp_metadata* metadata;

  ATFW_UTIL_FORCEINLINE ss_rpc_broadcast_target(const ss_msg_logic_index* input_index,
                                                atfw::atapp::protocol::atapp_metadata* input_metadata) noexcept
      : index(input_index), metadata(input_metadata) {}

  ATFW_UTIL_FORCEINLINE bool valid() const { return nullptr != index; }

  ATFW_UTIL_FORCEINLINE always_ready_code_type send(context&, atframework::SSMsg& message, uint64_t& sequence) const {
    auto result = ss_msg_dispatcher::me()->broadcast(message, *index, metadata);
    sequence = message.head().sequence();
    return {static_cast<always_ready_code_type::value_type>(result)};
  }
};

struct ATFW_UTIL_SYMBOL_VISIBLE ss_rpc_router_target {
  router_object_base::key_t key;
  router_manager_base* manager;

  // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor, misc-explicit-constructor)
  ATFW_UTIL_FORCEINLINE ss_rpc_router_target(const router_object_base::key_t& input_key,
                                             router_manager_base* input_manager = nullptr) noexcept
      : key(input_key), manager(input_manager) {}

  ATFW_UTIL_FORCEINLINE bool valid() const { return key.type_id != 0 && key.object_id != 0; }

  ATFW_UTIL_FORCEINLINE result_code_type send(context& ctx, atframework::SSMsg& message, uint64_t& sequence) const {
    // send_msg moves the envelope; callers must use its returned sequence after suspension.
    return manager->send_msg(ctx, key, std::move(message), sequence);
  }
};

template <class TTarget>
ATFW_UTIL_FORCEINLINE int prepare_ss_rpc_target(context&, const telemetry::tracer::span_ptr_type&,
                                                const ss_rpc_options&, TTarget&) {
  return 0;
}

ATFW_UTIL_FORCEINLINE int prepare_ss_rpc_target(context& ctx, const telemetry::tracer::span_ptr_type& span,
                                                const ss_rpc_options& options, ss_rpc_router_target& target) {
  if (!options.user_rpc && span) {
    span->SetAttribute("router_object.type_id", target.key.type_id);
    span->SetAttribute("router_object.zone_id", target.key.zone_id);
    span->SetAttribute("router_object.instance_id", target.key.object_id);
  }
  router_object_base::trace_router(ctx, target.key);
  target.manager = router_manager_set::me()->get_manager(target.key.type_id);
  if (nullptr == target.manager) {
    FCTXLOGERROR(ctx, "rpc {} can not get router manager of type {}", options.rpc_full_name, target.key.type_id);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOT_SUPPORT;
  }
  return 0;
}

template <class TTarget>
ATFW_UTIL_FORCEINLINE bool log_ss_rpc_ignored_result(context&, const ss_rpc_options&, const TTarget&, int, bool) {
  return false;
}

ATFW_UTIL_FORCEINLINE bool log_ss_rpc_ignored_result(context& ctx, const ss_rpc_options& options,
                                                     const ss_rpc_router_target& target, int result, bool stream) {
  if (stream && (result == PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND ||
                 result == PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER)) {
    FCTXLOGWARNING(ctx, "rpc {} call ignored for router cache {},{},{}: {}({})", options.rpc_full_name,
                   target.key.type_id, target.key.zone_id, target.key.object_id, result,
                   protobuf_mini_dumper_get_error_msg(result));
    return true;
  }
  return false;
}

template <class TRequest>
ATFW_UTIL_SYMBOL_VISIBLE int prepare_ss_rpc_request(context& ctx, TRequest& request_body, const ss_rpc_options& options,
                                                    bool stream, atframework::SSMsg*& message) {
  message = ctx.create<atframework::SSMsg>();
  if (nullptr == message) {
    FCTXLOGERROR(ctx, "rpc {} create request message failed", options.rpc_full_name);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }
  task_action_ss_req_base::init_msg(*message, logic_config::me()->get_local_server_id(),
                                    logic_config::me()->get_local_server_name());
  auto request_type = to_string_view(TRequest::descriptor()->full_name());
  int result = stream ? setup_rpc_stream_header(*message->mutable_head()->mutable_rpc_stream(),
                                                options.service_full_name, options.rpc_full_name, request_type)
                      : setup_ss_rpc_request_header(*message->mutable_head(), ctx.get_task_context().task_id,
                                                    options.service_full_name, options.rpc_full_name, request_type);
  if (result < 0) {
    return result;
  }
  return pack_rpc_body(request_body, message->mutable_body_bin(), options.rpc_full_name, request_type, false);
}

class ss_rpc_trace {
 public:
  ATFW_UTIL_FORCEINLINE ss_rpc_trace(context& parent, atframework::SSMsgHead& head, const ss_rpc_options& options)
      : child_context(parent),
        attributes{
            {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
            {
                opentelemetry::semconv::rpc::kRpcMethod,
                opentelemetry::nostd::string_view{options.rpc_full_name.data(), options.rpc_full_name.size()},
            },
        },
        span(setup_rpc_tracer(child_context, tracer, head, options.rpc_full_name, attributes)) {
    if (options.user_rpc) {
      head.set_user_user_id(options.user_id);
      head.set_user_zone_id(options.zone_id);
      if (span) {
        span->SetAttribute("user_id", options.user_id);
        span->SetAttribute("zone_id", options.zone_id);
      }
    }
  }

  template <class TTarget>
  ATFW_UTIL_FORCEINLINE int finish(int result, const ss_rpc_options& options, const TTarget& target, bool stream) {
    if (result < 0 && !log_ss_rpc_ignored_result(child_context, options, target, result, stream) &&
        !redirect_rpc_result_to_warning_log(result, options.warning_codes, options.rpc_full_name,
                                            options.response_type_full_name) &&
        !redirect_rpc_result_to_info_log(result, options.info_codes, options.rpc_full_name,
                                         options.response_type_full_name)) {
      FCTXLOGERROR(child_context, "rpc {} call failed, res: {}({})", options.rpc_full_name, result,
                   protobuf_mini_dumper_get_error_msg(result));
    }
    return tracer.finish({result, attributes});
  }

  context child_context;
  telemetry::tracer tracer;
  telemetry::trace_attribute_pair_type attributes[2];
  telemetry::tracer::span_ptr_type span;
};

// This path is synchronous even in a C++20 build, so callers need no coroutine task.
template <class TRequest, class TTarget>
ATFW_UTIL_SYMBOL_VISIBLE result_code_type::value_type send_ss_rpc(context& ctx, TRequest& request_body,
                                                                  ss_rpc_options options, TTarget target) {
  if (!target.valid()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }
  atframework::SSMsg* message = nullptr;
  int result = prepare_ss_rpc_request(ctx, request_body, options, true, message);
  if (result < 0) {
    return result;
  }
  ss_rpc_trace trace(ctx, *message->mutable_head(), options);
  uint64_t sequence = 0;
  result = target.send(ctx, *message, sequence).unwrap();
  FCTXLOGDEBUG(ctx, "{}", protobuf_mini_dumper_get_readable(request_body));
  return trace.finish(result, options, target, true);
}

template <class TResponseBody>
ATFW_UTIL_SYMBOL_VISIBLE result_code_type wait_and_unpack_ss_response(context& ctx, TResponseBody& response_body,
                                                                      atfw::util::nostd::string_view rpc_full_name,
                                                                      atfw::util::nostd::string_view type_full_name,
                                                                      dispatcher_await_options& await_options) {
  atframework::SSMsg* rsp_msg_ptr = ctx.create<atframework::SSMsg>();
  if (nullptr == rsp_msg_ptr) {
    FCTXLOGERROR(ctx, "rpc {} create response message failed", rpc_full_name);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  atframework::SSMsg& rsp_msg = *rsp_msg_ptr;
  result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, rsp_msg, await_options));

  if (rsp_msg.head().rpc_response().type_url() != type_full_name) {
    if (res >= 0 || !rsp_msg.head().rpc_response().type_url().empty()) {
      FCTXLOGERROR(ctx, "rpc {} expect response message {}, but got {}", rpc_full_name, type_full_name,
                   rsp_msg.head().rpc_response().type_url());
    }
    // 不把坏包当成功: 无显式错误码时按解包失败处理
    if (res >= 0 && rsp_msg.head().error_code() == 0) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
    }
  } else if (!rsp_msg.body_bin().empty()) {
    res = unpack_rpc_body(response_body, rsp_msg.body_bin(), rpc_full_name, type_full_name);
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  if (rsp_msg.has_head() && rsp_msg.head().error_code() != 0) {
    RPC_RETURN_CODE(rsp_msg.head().error_code());
  }
  RPC_RETURN_CODE(res);
}

template <class TRequest, class TResponse, class TTarget>
ATFW_UTIL_SYMBOL_VISIBLE result_code_type call_ss_rpc(context& ctx, TRequest& request_body, TResponse* response_body,
                                                      ss_rpc_options options, TTarget target, bool no_wait = false,
                                                      dispatcher_await_options* wait_later = nullptr) {
  if (!target.valid()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task", options.rpc_full_name)

  const bool stream = nullptr == response_body || no_wait;
  atframework::SSMsg* message = nullptr;
  int result = prepare_ss_rpc_request(ctx, request_body, options, stream, message);
  if (result < 0) {
    RPC_RETURN_CODE(result);
  }
  ss_rpc_trace trace(ctx, *message->mutable_head(), options);
  result = prepare_ss_rpc_target(trace.child_context, trace.span, options, target);
  if (result < 0) {
    RPC_RETURN_CODE(trace.tracer.finish({result, trace.attributes}));
  }
  uint64_t sequence = 0;
  result = RPC_AWAIT_CODE_RESULT(target.send(ctx, *message, sequence));
  FCTXLOGDEBUG(ctx, "{}", protobuf_mini_dumper_get_readable(request_body));

  if (nullptr != response_body) {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = sequence;
    auto default_timeout =
        make_duration_or_default(logic_config::me()->get_logic_cfg().task().csmsg().timeout(), std::chrono::seconds{6});
    if (nullptr != options.method && options.method->options().HasExtension(atframework::rpc_options)) {
      await_options.timeout = make_duration_or_default(
          options.method->options().GetExtension(atframework::rpc_options).timeout(), default_timeout);
    } else {
      await_options.timeout = default_timeout;
    }
    // Preserve no-wait precedence and publish wait-later options even when sending failed.
    if (!no_wait) {
      if (nullptr != wait_later) {
        *wait_later = await_options;
      } else if (result >= 0) {
        result = RPC_AWAIT_CODE_RESULT(wait_and_unpack_ss_response(ctx, *response_body, options.rpc_full_name,
                                                                   options.response_type_full_name, await_options));
      }
    }
  }
  // Only a declared stream RPC has the router-cache warning policy, not a unary no-wait call.
  RPC_RETURN_CODE(trace.finish(result, options, target, nullptr == response_body));
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
template <class TRequest, class TResponse>
ATFW_UTIL_SYMBOL_VISIBLE unit_test::mock_rule_handle register_ss_rpc_mock(
    gsl::string_view rpc_full_name, std::function<result_code_type(context&, const TRequest&, TResponse&)> handler,
    const unit_test::ss_mock_rule_options& options) {
  const auto& bridge = unit_test::get_mock_engine_bridge_for_unit_test();
  if (!bridge.register_ss_rule || !handler) {
    return unit_test::mock_rule_handle{};
  }
  return unit_test::mock_rule_handle{
      bridge.register_ss_rule(
          rpc_full_name, TRequest::descriptor()->full_name(), TResponse::descriptor()->full_name(),
          [handler = std::move(handler)](const unit_test::ss_mock_request_view& view,
                                         google::protobuf::Message& response) -> result_code_type {
            if (nullptr == view.body || nullptr == view.context) {
              RPC_RETURN_CODE(-1);
            }
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                handler(*view.context, static_cast<const TRequest&>(*view.body), static_cast<TResponse&>(response))));
          },
          options),
  };
}
#endif
}  // namespace internal
}  // namespace rpc
