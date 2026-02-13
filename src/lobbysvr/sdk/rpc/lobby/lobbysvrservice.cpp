// Copyright 2026 atframework
// @brief Created by mako-generator.py for hello.LobbysvrService, please don't edit it

#include "lobbysvrservice.h"

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format offc
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <atframe/etcdcli/etcd_discovery.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_action_ss_req_base.h>
#include <router/router_manager_base.h>
#include <router/router_manager_set.h>
#include <router/router_object_base.h>
#include <router/router_player_manager.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
namespace {
ATFW_UTIL_FORCEINLINE static bool __is_invalid_server_node(const atfw::atapp::etcd_discovery_node& destination_server) {
  return destination_server.get_discovery_info().id() == 0 || destination_server.get_discovery_info().name().empty();
}

ATFW_UTIL_FORCEINLINE static bool __is_invalid_server_node(uint64_t destination_server) {
  return destination_server == 0;
}

template <class StringViewLikeT>
inline static atfw::util::nostd::string_view __to_string_view(const StringViewLikeT& input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}

template <class TBodyType>
inline static int __pack_rpc_body(TBodyType&& input, std::string* output, atfw::util::nostd::string_view rpc_full_name,
                                  atfw::util::nostd::string_view type_full_name) {
  if (false == input.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               input.InitializationErrorString());
    return hello::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
               protobuf_mini_dumper_get_readable(input));
    return hello::err::EN_SUCCESS;
  }
}

template <class TBodyType>
inline static int __unpack_rpc_body(TBodyType&& output, const std::string& input,
                                    atfw::util::nostd::string_view rpc_full_name,
                                    atfw::util::nostd::string_view type_full_name) {
  if (false == output.ParseFromString(input)) {
    FWLOGERROR("rpc {} parse message {} failed, msg: {}", rpc_full_name, type_full_name,
               output.InitializationErrorString());
    return hello::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name,
               protobuf_mini_dumper_get_readable(output));
    return hello::err::EN_SUCCESS;
  }
}

inline static rpc::telemetry::tracer::span_ptr_type __setup_tracer(rpc::context& __child_ctx,
                                                                   rpc::telemetry::tracer& __tracer,
                                                                   atframework::SSMsgHead& head,
                                                                   atfw::util::nostd::string_view rpc_full_name,
                                                                   rpc::telemetry::trace_attributes_type attributes) {
  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = attributes;

  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/semantic_conventions/README.md
  __child_ctx.setup_tracer(__tracer, rpc::context::string_view{rpc_full_name.data(), rpc_full_name.size()},
                           std::move(__trace_option));
  if (__tracer.is_recording()) {
    rpc::telemetry::tracer::span_ptr_type __child_trace_span = __child_ctx.get_trace_span();
    if (__child_trace_span) {
      auto trace_span_head = head.mutable_rpc_trace();
      if (trace_span_head) {
        auto trace_context = __child_trace_span->GetContext();
        rpc::telemetry::tracer::trace_id_span trace_id = trace_context.trace_id().Id();
        rpc::telemetry::tracer::span_id_span span_id = trace_context.span_id().Id();

        trace_span_head->mutable_trace_id()->assign(reinterpret_cast<const char*>(trace_id.data()), trace_id.size());
        trace_span_head->mutable_span_id()->assign(reinterpret_cast<const char*>(span_id.data()), span_id.size());
        trace_span_head->set_kind(__trace_option.kind);
        trace_span_head->set_name(static_cast<std::string>(rpc_full_name));
      }
    }

    return __child_trace_span;
  } else {
    auto trace_span_head = head.mutable_rpc_trace();
    if (trace_span_head) {
      trace_span_head->set_dynamic_ignore(true);
    }

    return rpc::telemetry::tracer::span_ptr_type();
  }
}

inline static int __setup_rpc_stream_header(atframework::SSMsgHead& head, atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name) {
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return hello::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee("hello.LobbysvrService");
  stream_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  stream_meta->set_type_url(type_full_name.data(), type_full_name.size());
  stream_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  stream_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return hello::err::EN_SUCCESS;
}
inline static int __setup_rpc_request_header(atframework::SSMsgHead& head, task_type_trait::id_type task_id,
                                             atfw::util::nostd::string_view rpc_full_name,
                                             atfw::util::nostd::string_view type_full_name) {
  head.set_source_task_id(task_id);
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return hello::err::EN_SYS_MALLOC;
  }
  request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  request_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  request_meta->set_callee("hello.LobbysvrService");
  request_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  request_meta->set_type_url(type_full_name.data(), type_full_name.size());
  request_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  request_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return hello::err::EN_SUCCESS;
}
template <class TResponseBody>
inline static rpc::result_code_type __rpc_wait_and_unpack_response(rpc::context& __ctx, TResponseBody& response_body,
                                                                   atfw::util::nostd::string_view rpc_full_name,
                                                                   atfw::util::nostd::string_view type_full_name,
                                                                   dispatcher_await_options& await_options) {
  atframework::SSMsg* rsp_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == rsp_msg_ptr) {
    FWLOGERROR("rpc {} create response message failed", rpc_full_name);
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  atframework::SSMsg& rsp_msg = *rsp_msg_ptr;
  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(rpc::wait(__ctx, rsp_msg, await_options));

  if (rsp_msg.head().rpc_response().type_url() != type_full_name) {
    if (res >= 0 || !rsp_msg.head().rpc_response().type_url().empty()) {
      FWLOGERROR("rpc {} expect response message {}, but got {}", rpc_full_name, type_full_name,
                 rsp_msg.head().rpc_response().type_url());
    }
  } else if (!rsp_msg.body_bin().empty()) {
    res = __unpack_rpc_body(response_body, rsp_msg.body_bin(), rpc_full_name, type_full_name);
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  if (rsp_msg.has_head() && rsp_msg.head().error_code() != 0) {
    RPC_RETURN_CODE(rsp_msg.head().error_code());
  }
  RPC_RETURN_CODE(res);
}
}  // namespace

namespace lobby {

// ============ hello.LobbysvrService.player_kickoff ============
namespace packer {
GAME_RPC_API bool pack_player_kickoff(std::string& output, const hello::SSPlayerKickOffReq& input) {
  return hello::err::EN_SUCCESS ==
         __pack_rpc_body(input, &output, "hello.LobbysvrService.player_kickoff",
                         __to_string_view(hello::SSPlayerKickOffReq::descriptor()->full_name()));
}

GAME_RPC_API bool unpack_player_kickoff(const std::string& input, hello::SSPlayerKickOffReq& output) {
  return hello::err::EN_SUCCESS ==
         __unpack_rpc_body(output, input, "hello.LobbysvrService.player_kickoff",
                           __to_string_view(hello::SSPlayerKickOffReq::descriptor()->full_name()));
}

GAME_RPC_API bool pack_player_kickoff(std::string& output, const hello::SSPlayerKickOffRsp& input) {
  return hello::err::EN_SUCCESS ==
         __pack_rpc_body(input, &output, "hello.LobbysvrService.player_kickoff",
                         __to_string_view(hello::SSPlayerKickOffRsp::descriptor()->full_name()));
}

GAME_RPC_API bool unpack_player_kickoff(const std::string& input, hello::SSPlayerKickOffRsp& output) {
  return hello::err::EN_SUCCESS ==
         __unpack_rpc_body(output, input, "hello.LobbysvrService.player_kickoff",
                           __to_string_view(hello::SSPlayerKickOffRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template <class TargetServerNode>
static rpc::result_code_type __player_kickoff(context& __ctx, TargetServerNode&& destination_server, uint32_t zone_id,
                                              uint64_t user_id, const std::string& open_id,
                                              hello::SSPlayerKickOffReq& request_body,
                                              hello::SSPlayerKickOffRsp& response_body) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(hello::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task", "hello.LobbysvrService.player_kickoff")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed", "hello.LobbysvrService.player_kickoff");
    RPC_RETURN_CODE(hello::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
                                    logic_config::me()->get_local_server_name());
  res = __setup_rpc_request_header(*req_msg.mutable_head(), __ctx.get_task_context().task_id,
                                   "hello.LobbysvrService.player_kickoff",
                                   __to_string_view(hello::SSPlayerKickOffReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(request_body, req_msg.mutable_body_bin(), "hello.LobbysvrService.player_kickoff",
                        __to_string_view(hello::SSPlayerKickOffReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcService, "hello.LobbysvrService"},
      {opentelemetry::semconv::rpc::kRpcMethod, "hello.LobbysvrService.player_kickoff"}};
  auto __child_trace_span = __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                                           "hello.LobbysvrService.player_kickoff", __trace_attributes);

  req_msg.mutable_head()->set_player_user_id(user_id);
  req_msg.mutable_head()->set_player_zone_id(zone_id);
  req_msg.mutable_head()->set_player_open_id(open_id);
  if (__child_trace_span) {
    __child_trace_span->SetAttribute("user_id", user_id);
    __child_trace_span->SetAttribute("zone_id", zone_id);
  }
  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor* method =
          hello::LobbysvrService::descriptor()->FindMethodByName("player_kickoff");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_logic().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(
        __ctx, response_body, "hello.LobbysvrService.player_kickoff",
        __to_string_view(hello::SSPlayerKickOffRsp::descriptor()->full_name()), await_options));
  } while (false);

  if (res < 0) {
    FWLOGERROR("rpc {} call failed, res: {}({})", "hello.LobbysvrService.player_kickoff", res,
               protobuf_mini_dumper_get_error_msg(res));
  }

  RPC_RETURN_CODE(__tracer.finish({res, __trace_attributes}));
}

GAME_RPC_API rpc::result_code_type player_kickoff(context& __ctx,
                                                  const atfw::atapp::etcd_discovery_node& destination_server,
                                                  uint32_t zone_id, uint64_t user_id, const std::string& open_id,
                                                  hello::SSPlayerKickOffReq& request_body,
                                                  hello::SSPlayerKickOffRsp& response_body) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      __player_kickoff(__ctx, destination_server, zone_id, user_id, open_id, request_body, response_body)));
}
}  // namespace unicast

GAME_RPC_API rpc::result_code_type player_kickoff(context& __ctx, uint64_t destination_server, uint32_t zone_id,
                                                  uint64_t user_id, const std::string& open_id,
                                                  hello::SSPlayerKickOffReq& request_body,
                                                  hello::SSPlayerKickOffRsp& response_body) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      unicast::__player_kickoff(__ctx, destination_server, zone_id, user_id, open_id, request_body, response_body)));
}

// ============ hello.LobbysvrService.player_async_jobs_sync ============
namespace packer {
GAME_RPC_API bool pack_player_async_jobs_sync(std::string& output, const hello::SSPlayerAsyncJobsSync& input) {
  return hello::err::EN_SUCCESS ==
         __pack_rpc_body(input, &output, "hello.LobbysvrService.player_async_jobs_sync",
                         __to_string_view(hello::SSPlayerAsyncJobsSync::descriptor()->full_name()));
}

GAME_RPC_API bool unpack_player_async_jobs_sync(const std::string& input, hello::SSPlayerAsyncJobsSync& output) {
  return hello::err::EN_SUCCESS ==
         __unpack_rpc_body(output, input, "hello.LobbysvrService.player_async_jobs_sync",
                           __to_string_view(hello::SSPlayerAsyncJobsSync::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template <class TargetServerNode>
static rpc::always_ready_code_type __player_async_jobs_sync(context& __ctx, TargetServerNode&& destination_server,
                                                            uint32_t zone_id, uint64_t user_id,
                                                            const std::string& open_id,
                                                            hello::SSPlayerAsyncJobsSync& request_body) {
  if (__is_invalid_server_node(destination_server)) {
    return {static_cast<rpc::always_ready_code_type::value_type>(hello::err::EN_SYS_PARAM)};
  }

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed", "hello.LobbysvrService.player_async_jobs_sync");
    return {static_cast<rpc::always_ready_code_type::value_type>(hello::err::EN_SYS_MALLOC)};
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
                                    logic_config::me()->get_local_server_name());
  res = __setup_rpc_stream_header(*req_msg.mutable_head(), "hello.LobbysvrService.player_async_jobs_sync",
                                  __to_string_view(hello::SSPlayerAsyncJobsSync::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_rpc_body(request_body, req_msg.mutable_body_bin(), "hello.LobbysvrService.player_async_jobs_sync",
                        __to_string_view(hello::SSPlayerAsyncJobsSync::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
      {opentelemetry::semconv::rpc::kRpcService, "hello.LobbysvrService"},
      {opentelemetry::semconv::rpc::kRpcMethod, "hello.LobbysvrService.player_async_jobs_sync"}};
  auto __child_trace_span = __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                                           "hello.LobbysvrService.player_async_jobs_sync", __trace_attributes);

  req_msg.mutable_head()->set_player_user_id(user_id);
  req_msg.mutable_head()->set_player_zone_id(zone_id);
  req_msg.mutable_head()->set_player_open_id(open_id);
  if (__child_trace_span) {
    __child_trace_span->SetAttribute("user_id", user_id);
    __child_trace_span->SetAttribute("zone_id", zone_id);
  }
  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  if (res < 0) {
    FWLOGERROR("rpc {} call failed, res: {}({})", "hello.LobbysvrService.player_async_jobs_sync", res,
               protobuf_mini_dumper_get_error_msg(res));
  }
  return {static_cast<rpc::always_ready_code_type::value_type>(__tracer.finish({res, __trace_attributes}))};
}

GAME_RPC_API rpc::always_ready_code_type player_async_jobs_sync(
    context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, uint32_t zone_id, uint64_t user_id,
    const std::string& open_id, hello::SSPlayerAsyncJobsSync& request_body) {
  return __player_async_jobs_sync(__ctx, destination_server, zone_id, user_id, open_id, request_body);
}
}  // namespace unicast

GAME_RPC_API rpc::always_ready_code_type player_async_jobs_sync(context& __ctx, uint64_t destination_server,
                                                                uint32_t zone_id, uint64_t user_id,
                                                                const std::string& open_id,
                                                                hello::SSPlayerAsyncJobsSync& request_body) {
  return unicast::__player_async_jobs_sync(__ctx, destination_server, zone_id, user_id, open_id, request_body);
}

}  // namespace lobby
}  // namespace rpc
