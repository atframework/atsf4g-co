// Copyright 2026 atframework
// @brief Created by mako-generator.py for prx.RanksvrService, please don't edit it

#include "ranksvrservice.h"

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>
#include <protocol/pbdesc/rank_service.pb.h>

// clang-format offc
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <atframe/etcdcli/etcd_discovery.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <router/router_manager_set.h>
#include <router/router_manager_base.h>
#include <router/router_player_manager.h>
#include <router/router_object_base.h>

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

template<class StringViewLikeT>
inline static atfw::util::nostd::string_view __to_string_view(const StringViewLikeT &input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}

template<class TBodyType>
inline static int __pack_rpc_body(TBodyType &&input, std::string *output, atfw::util::nostd::string_view rpc_full_name,
                                atfw::util::nostd::string_view type_full_name) {
  if (false == input.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
              input.InitializationErrorString());
    return prx::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
              protobuf_mini_dumper_get_readable(input));
    return prx::err::EN_SUCCESS;
  }
}

template<class TBodyType>
inline static int __unpack_rpc_body(TBodyType &&output, const std::string& input,
                                atfw::util::nostd::string_view rpc_full_name,
                                atfw::util::nostd::string_view type_full_name) {
  if (false == output.ParseFromString(input)) {
    FWLOGERROR("rpc {} parse message {} failed, msg: {}", rpc_full_name, type_full_name,
              output.InitializationErrorString());
    return prx::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name,
              protobuf_mini_dumper_get_readable(output));
    return prx::err::EN_SUCCESS;
  }
}

inline static rpc::telemetry::tracer::span_ptr_type __setup_tracer(rpc::context &__child_ctx,
                                  rpc::telemetry::tracer &__tracer, atframework::SSMsgHead &head,
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

        trace_span_head->mutable_trace_id()->assign(reinterpret_cast<const char *>(trace_id.data()), trace_id.size());
        trace_span_head->mutable_span_id()->assign(reinterpret_cast<const char *>(span_id.data()), span_id.size());
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

inline static int __setup_rpc_stream_header(atframework::SSMsgHead &head, atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name) {
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return prx::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee("prx.RanksvrService");
  stream_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  stream_meta->set_type_url(type_full_name.data(), type_full_name.size());
  stream_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  stream_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return prx::err::EN_SUCCESS;
}
inline static int __setup_rpc_request_header(atframework::SSMsgHead &head, task_type_trait::id_type task_id,
                                             atfw::util::nostd::string_view rpc_full_name,
                                             atfw::util::nostd::string_view type_full_name) {
  head.set_source_task_id(task_id);
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return prx::err::EN_SYS_MALLOC;
  }
  request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  request_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  request_meta->set_callee("prx.RanksvrService");
  request_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  request_meta->set_type_url(type_full_name.data(), type_full_name.size());
  request_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  request_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return prx::err::EN_SUCCESS;
}
template<class TResponseBody>
inline static rpc::result_code_type __rpc_wait_and_unpack_response(rpc::context &__ctx, TResponseBody &response_body,
                                            atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name,
                                            dispatcher_await_options& await_options) {
  atframework::SSMsg* rsp_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == rsp_msg_ptr) {
    FWLOGERROR("rpc {} create response message failed", rpc_full_name);
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
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
    if(res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  if (rsp_msg.has_head() && rsp_msg.head().error_code() != 0) {
    RPC_RETURN_CODE(rsp_msg.head().error_code());
  }
  RPC_RETURN_CODE(res);
}
}  // namespace

namespace rank {

// ============ prx.RanksvrService.rank_get_special ============
namespace packer {
RANK_SDK_API bool pack_rank_get_special(std::string& output, const prx::SSRankGetSpecifyRankReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_get_special",
             __to_string_view(prx::SSRankGetSpecifyRankReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_get_special(const std::string& input, prx::SSRankGetSpecifyRankReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_get_special",
             __to_string_view(prx::SSRankGetSpecifyRankReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_get_special(std::string& output, const prx::SSRankGetSpecifyRankRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_get_special",
             __to_string_view(prx::SSRankGetSpecifyRankRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_get_special(const std::string& input, prx::SSRankGetSpecifyRankRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_get_special",
             __to_string_view(prx::SSRankGetSpecifyRankRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_get_special(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankGetSpecifyRankReq &request_body, prx::SSRankGetSpecifyRankRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_get_special")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_get_special");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_get_special",
    __to_string_view(  prx::SSRankGetSpecifyRankReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_get_special",
    __to_string_view(  prx::SSRankGetSpecifyRankReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_get_special",
    __to_string_view(prx::SSRankGetSpecifyRankReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_get_special"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_get_special",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_get_special");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_get_special",
        __to_string_view(prx::SSRankGetSpecifyRankRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_get_special",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_get_special(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankGetSpecifyRankReq &request_body, prx::SSRankGetSpecifyRankRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_get_special(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_get_special(
  context& __ctx, uint64_t destination_server, prx::SSRankGetSpecifyRankReq &request_body, prx::SSRankGetSpecifyRankRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_get_special(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_set_score ============
namespace packer {
RANK_SDK_API bool pack_rank_set_score(std::string& output, const prx::SSRankSetScoreReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_set_score",
             __to_string_view(prx::SSRankSetScoreReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_set_score(const std::string& input, prx::SSRankSetScoreReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_set_score",
             __to_string_view(prx::SSRankSetScoreReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_set_score(std::string& output, const prx::SSRankSetScoreRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_set_score",
             __to_string_view(prx::SSRankSetScoreRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_set_score(const std::string& input, prx::SSRankSetScoreRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_set_score",
             __to_string_view(prx::SSRankSetScoreRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_set_score(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankSetScoreReq &request_body, prx::SSRankSetScoreRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_set_score")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_set_score");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_set_score",
    __to_string_view(  prx::SSRankSetScoreReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_set_score",
    __to_string_view(  prx::SSRankSetScoreReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_set_score",
    __to_string_view(prx::SSRankSetScoreReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_set_score"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_set_score",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_set_score");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_set_score",
        __to_string_view(prx::SSRankSetScoreRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_set_score",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_set_score(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankSetScoreReq &request_body, prx::SSRankSetScoreRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_set_score(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_set_score(
  context& __ctx, uint64_t destination_server, prx::SSRankSetScoreReq &request_body, prx::SSRankSetScoreRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_set_score(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_modify_score ============
namespace packer {
RANK_SDK_API bool pack_rank_modify_score(std::string& output, const prx::SSRankModifyScoreReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_modify_score",
             __to_string_view(prx::SSRankModifyScoreReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_modify_score(const std::string& input, prx::SSRankModifyScoreReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_modify_score",
             __to_string_view(prx::SSRankModifyScoreReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_modify_score(std::string& output, const prx::SSRankModifyScoreRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_modify_score",
             __to_string_view(prx::SSRankModifyScoreRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_modify_score(const std::string& input, prx::SSRankModifyScoreRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_modify_score",
             __to_string_view(prx::SSRankModifyScoreRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_modify_score(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankModifyScoreReq &request_body, prx::SSRankModifyScoreRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_modify_score")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_modify_score");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_modify_score",
    __to_string_view(  prx::SSRankModifyScoreReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_modify_score",
    __to_string_view(  prx::SSRankModifyScoreReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_modify_score",
    __to_string_view(prx::SSRankModifyScoreReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_modify_score"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_modify_score",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_modify_score");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_modify_score",
        __to_string_view(prx::SSRankModifyScoreRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_modify_score",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_modify_score(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankModifyScoreReq &request_body, prx::SSRankModifyScoreRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_modify_score(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_modify_score(
  context& __ctx, uint64_t destination_server, prx::SSRankModifyScoreReq &request_body, prx::SSRankModifyScoreRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_modify_score(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_del_one_user ============
namespace packer {
RANK_SDK_API bool pack_rank_del_one_user(std::string& output, const prx::SSRankDelUserReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_del_one_user",
             __to_string_view(prx::SSRankDelUserReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_del_one_user(const std::string& input, prx::SSRankDelUserReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_del_one_user",
             __to_string_view(prx::SSRankDelUserReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_del_one_user(std::string& output, const prx::SSRankDelUserRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_del_one_user",
             __to_string_view(prx::SSRankDelUserRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_del_one_user(const std::string& input, prx::SSRankDelUserRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_del_one_user",
             __to_string_view(prx::SSRankDelUserRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_del_one_user(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankDelUserReq &request_body, prx::SSRankDelUserRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_del_one_user")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_del_one_user");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_del_one_user",
    __to_string_view(  prx::SSRankDelUserReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_del_one_user",
    __to_string_view(  prx::SSRankDelUserReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_del_one_user",
    __to_string_view(prx::SSRankDelUserReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_del_one_user"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_del_one_user",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_del_one_user");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_del_one_user",
        __to_string_view(prx::SSRankDelUserRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_del_one_user",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_del_one_user(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankDelUserReq &request_body, prx::SSRankDelUserRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_del_one_user(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_del_one_user(
  context& __ctx, uint64_t destination_server, prx::SSRankDelUserReq &request_body, prx::SSRankDelUserRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_del_one_user(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_get_top ============
namespace packer {
RANK_SDK_API bool pack_rank_get_top(std::string& output, const prx::SSRankGetTopReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_get_top",
             __to_string_view(prx::SSRankGetTopReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_get_top(const std::string& input, prx::SSRankGetTopReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_get_top",
             __to_string_view(prx::SSRankGetTopReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_get_top(std::string& output, const prx::SSRankGetTopRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_get_top",
             __to_string_view(prx::SSRankGetTopRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_get_top(const std::string& input, prx::SSRankGetTopRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_get_top",
             __to_string_view(prx::SSRankGetTopRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_get_top(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankGetTopReq &request_body, prx::SSRankGetTopRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_get_top")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_get_top");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_get_top",
    __to_string_view(  prx::SSRankGetTopReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_get_top",
    __to_string_view(  prx::SSRankGetTopReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_get_top",
    __to_string_view(prx::SSRankGetTopReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_get_top"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_get_top",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_get_top");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_get_top",
        __to_string_view(prx::SSRankGetTopRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_get_top",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_get_top(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankGetTopReq &request_body, prx::SSRankGetTopRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_get_top(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_get_top(
  context& __ctx, uint64_t destination_server, prx::SSRankGetTopReq &request_body, prx::SSRankGetTopRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_get_top(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_clear ============
namespace packer {
RANK_SDK_API bool pack_rank_clear(std::string& output, const prx::SSRankClearReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_clear",
             __to_string_view(prx::SSRankClearReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_clear(const std::string& input, prx::SSRankClearReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_clear",
             __to_string_view(prx::SSRankClearReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_clear(std::string& output, const prx::SSRankClearRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_clear",
             __to_string_view(prx::SSRankClearRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_clear(const std::string& input, prx::SSRankClearRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_clear",
             __to_string_view(prx::SSRankClearRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_clear(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankClearReq &request_body, prx::SSRankClearRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_clear")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_clear");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_clear",
    __to_string_view(  prx::SSRankClearReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_clear",
    __to_string_view(  prx::SSRankClearReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_clear",
    __to_string_view(prx::SSRankClearReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_clear"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_clear",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_clear");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_clear",
        __to_string_view(prx::SSRankClearRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_clear",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_clear(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankClearReq &request_body, prx::SSRankClearRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_clear(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_clear(
  context& __ctx, uint64_t destination_server, prx::SSRankClearReq &request_body, prx::SSRankClearRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_clear(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_load_main ============
namespace packer {
RANK_SDK_API bool pack_rank_load_main(std::string& output, const prx::SSRankLoadMainReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_load_main",
             __to_string_view(prx::SSRankLoadMainReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_load_main(const std::string& input, prx::SSRankLoadMainReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_load_main",
             __to_string_view(prx::SSRankLoadMainReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_load_main(std::string& output, const prx::SSRankLoadMainRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_load_main",
             __to_string_view(prx::SSRankLoadMainRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_load_main(const std::string& input, prx::SSRankLoadMainRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_load_main",
             __to_string_view(prx::SSRankLoadMainRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_load_main(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankLoadMainReq &request_body, prx::SSRankLoadMainRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_load_main")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_load_main");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_load_main",
    __to_string_view(  prx::SSRankLoadMainReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_load_main",
    __to_string_view(  prx::SSRankLoadMainReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_load_main",
    __to_string_view(prx::SSRankLoadMainReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_load_main"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_load_main",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_load_main");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_load_main",
        __to_string_view(prx::SSRankLoadMainRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_load_main",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_load_main(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankLoadMainReq &request_body, prx::SSRankLoadMainRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_load_main(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_load_main(
  context& __ctx, uint64_t destination_server, prx::SSRankLoadMainReq &request_body, prx::SSRankLoadMainRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_load_main(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_check_slave ============
namespace packer {
RANK_SDK_API bool pack_rank_check_slave(std::string& output, const prx::SSRankCheckSlaveReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_check_slave",
             __to_string_view(prx::SSRankCheckSlaveReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_check_slave(const std::string& input, prx::SSRankCheckSlaveReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_check_slave",
             __to_string_view(prx::SSRankCheckSlaveReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_check_slave(std::string& output, const prx::SSRankCheckSlaveRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_check_slave",
             __to_string_view(prx::SSRankCheckSlaveRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_check_slave(const std::string& input, prx::SSRankCheckSlaveRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_check_slave",
             __to_string_view(prx::SSRankCheckSlaveRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_check_slave(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankCheckSlaveReq &request_body, prx::SSRankCheckSlaveRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_check_slave")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_check_slave");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_check_slave",
    __to_string_view(  prx::SSRankCheckSlaveReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_check_slave",
    __to_string_view(  prx::SSRankCheckSlaveReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_check_slave",
    __to_string_view(prx::SSRankCheckSlaveReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_check_slave"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_check_slave",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_check_slave");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_check_slave",
        __to_string_view(prx::SSRankCheckSlaveRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_check_slave",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_check_slave(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankCheckSlaveReq &request_body, prx::SSRankCheckSlaveRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_check_slave(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_check_slave(
  context& __ctx, uint64_t destination_server, prx::SSRankCheckSlaveReq &request_body, prx::SSRankCheckSlaveRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_check_slave(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_event_sync ============
namespace packer {
RANK_SDK_API bool pack_rank_event_sync(std::string& output, const prx::SSRankEventSync& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_event_sync",
             __to_string_view(prx::SSRankEventSync::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_event_sync(const std::string& input, prx::SSRankEventSync& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_event_sync",
             __to_string_view(prx::SSRankEventSync::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::always_ready_code_type __rank_event_sync(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankEventSync &request_body) {
  if (__is_invalid_server_node(destination_server)) {
    return {static_cast<rpc::always_ready_code_type::value_type>(prx::err::EN_SYS_PARAM)};
  }


  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_event_sync");
    return {static_cast<rpc::always_ready_code_type::value_type>(prx::err::EN_SYS_MALLOC)};
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  res = __setup_rpc_stream_header(
    *req_msg.mutable_head(), "prx.RanksvrService.rank_event_sync",
    __to_string_view(prx::SSRankEventSync::descriptor()->full_name())
  );
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_event_sync",
    __to_string_view(prx::SSRankEventSync::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_event_sync"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_event_sync",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  if (res < 0) {
    FWLOGERROR("rpc {} call failed, res: {}({})",
               "prx.RanksvrService.rank_event_sync",
               res, protobuf_mini_dumper_get_error_msg(res)
    );
  }
  return {static_cast<rpc::always_ready_code_type::value_type>(__tracer.finish({res , __trace_attributes}))};
}

RANK_SDK_API rpc::always_ready_code_type rank_event_sync(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankEventSync &request_body) {
  return __rank_event_sync(__ctx, destination_server, request_body);
}
}  // namespace unicast

RANK_SDK_API rpc::always_ready_code_type rank_event_sync(
  context& __ctx, uint64_t destination_server, prx::SSRankEventSync &request_body) {
  return unicast::__rank_event_sync(__ctx, destination_server, request_body);
}


// ============ prx.RanksvrService.rank_heartbeat ============
namespace packer {
RANK_SDK_API bool pack_rank_heartbeat(std::string& output, const prx::SSRankHeartbeatReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_heartbeat",
             __to_string_view(prx::SSRankHeartbeatReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_heartbeat(const std::string& input, prx::SSRankHeartbeatReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_heartbeat",
             __to_string_view(prx::SSRankHeartbeatReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_heartbeat(std::string& output, const prx::SSRankHeartbeatRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_heartbeat",
             __to_string_view(prx::SSRankHeartbeatRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_heartbeat(const std::string& input, prx::SSRankHeartbeatRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_heartbeat",
             __to_string_view(prx::SSRankHeartbeatRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_heartbeat(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankHeartbeatReq &request_body, prx::SSRankHeartbeatRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_heartbeat")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_heartbeat");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_heartbeat",
    __to_string_view(  prx::SSRankHeartbeatReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_heartbeat",
    __to_string_view(  prx::SSRankHeartbeatReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_heartbeat",
    __to_string_view(prx::SSRankHeartbeatReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_heartbeat"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_heartbeat",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_heartbeat");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_heartbeat",
        __to_string_view(prx::SSRankHeartbeatRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_heartbeat",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_heartbeat(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankHeartbeatReq &request_body, prx::SSRankHeartbeatRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_heartbeat(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_heartbeat(
  context& __ctx, uint64_t destination_server, prx::SSRankHeartbeatReq &request_body, prx::SSRankHeartbeatRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_heartbeat(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_switch_to_slave ============
namespace packer {
RANK_SDK_API bool pack_rank_switch_to_slave(std::string& output, const prx::SSRankSwitchToSlaveReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_switch_to_slave",
             __to_string_view(prx::SSRankSwitchToSlaveReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_switch_to_slave(const std::string& input, prx::SSRankSwitchToSlaveReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_switch_to_slave",
             __to_string_view(prx::SSRankSwitchToSlaveReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_switch_to_slave(std::string& output, const prx::SSRankSwitchToSlaveRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_switch_to_slave",
             __to_string_view(prx::SSRankSwitchToSlaveRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_switch_to_slave(const std::string& input, prx::SSRankSwitchToSlaveRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_switch_to_slave",
             __to_string_view(prx::SSRankSwitchToSlaveRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_switch_to_slave(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankSwitchToSlaveReq &request_body, prx::SSRankSwitchToSlaveRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_switch_to_slave")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_switch_to_slave");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_switch_to_slave",
    __to_string_view(  prx::SSRankSwitchToSlaveReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_switch_to_slave",
    __to_string_view(  prx::SSRankSwitchToSlaveReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_switch_to_slave",
    __to_string_view(prx::SSRankSwitchToSlaveReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_switch_to_slave"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_switch_to_slave",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_switch_to_slave");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_switch_to_slave",
        __to_string_view(prx::SSRankSwitchToSlaveRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_switch_to_slave",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_switch_to_slave(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankSwitchToSlaveReq &request_body, prx::SSRankSwitchToSlaveRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_switch_to_slave(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_switch_to_slave(
  context& __ctx, uint64_t destination_server, prx::SSRankSwitchToSlaveReq &request_body, prx::SSRankSwitchToSlaveRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_switch_to_slave(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_get_special_one_front_back ============
namespace packer {
RANK_SDK_API bool pack_rank_get_special_one_front_back(std::string& output, const prx::SSRankGetUserFrontBackReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_get_special_one_front_back",
             __to_string_view(prx::SSRankGetUserFrontBackReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_get_special_one_front_back(const std::string& input, prx::SSRankGetUserFrontBackReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_get_special_one_front_back",
             __to_string_view(prx::SSRankGetUserFrontBackReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_get_special_one_front_back(std::string& output, const prx::SSRankGetUserFrontBackRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_get_special_one_front_back",
             __to_string_view(prx::SSRankGetUserFrontBackRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_get_special_one_front_back(const std::string& input, prx::SSRankGetUserFrontBackRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_get_special_one_front_back",
             __to_string_view(prx::SSRankGetUserFrontBackRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_get_special_one_front_back(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankGetUserFrontBackReq &request_body, prx::SSRankGetUserFrontBackRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_get_special_one_front_back")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_get_special_one_front_back");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_get_special_one_front_back",
    __to_string_view(  prx::SSRankGetUserFrontBackReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_get_special_one_front_back",
    __to_string_view(  prx::SSRankGetUserFrontBackReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_get_special_one_front_back",
    __to_string_view(prx::SSRankGetUserFrontBackReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_get_special_one_front_back"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_get_special_one_front_back",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_get_special_one_front_back");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_get_special_one_front_back",
        __to_string_view(prx::SSRankGetUserFrontBackRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_get_special_one_front_back",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_get_special_one_front_back(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankGetUserFrontBackReq &request_body, prx::SSRankGetUserFrontBackRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_get_special_one_front_back(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_get_special_one_front_back(
  context& __ctx, uint64_t destination_server, prx::SSRankGetUserFrontBackReq &request_body, prx::SSRankGetUserFrontBackRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_get_special_one_front_back(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_make_new_mirror ============
namespace packer {
RANK_SDK_API bool pack_rank_make_new_mirror(std::string& output, const prx::SSRankMakeNewMirrorReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_make_new_mirror",
             __to_string_view(prx::SSRankMakeNewMirrorReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_make_new_mirror(const std::string& input, prx::SSRankMakeNewMirrorReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_make_new_mirror",
             __to_string_view(prx::SSRankMakeNewMirrorReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_make_new_mirror(std::string& output, const prx::SSRankMakeNewMirrorRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_make_new_mirror",
             __to_string_view(prx::SSRankMakeNewMirrorRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_make_new_mirror(const std::string& input, prx::SSRankMakeNewMirrorRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_make_new_mirror",
             __to_string_view(prx::SSRankMakeNewMirrorRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_make_new_mirror(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankMakeNewMirrorReq &request_body, prx::SSRankMakeNewMirrorRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_make_new_mirror")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_make_new_mirror");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_make_new_mirror",
    __to_string_view(  prx::SSRankMakeNewMirrorReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_make_new_mirror",
    __to_string_view(  prx::SSRankMakeNewMirrorReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_make_new_mirror",
    __to_string_view(prx::SSRankMakeNewMirrorReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_make_new_mirror"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_make_new_mirror",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_make_new_mirror");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_make_new_mirror",
        __to_string_view(prx::SSRankMakeNewMirrorRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_make_new_mirror",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_make_new_mirror(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankMakeNewMirrorReq &request_body, prx::SSRankMakeNewMirrorRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_make_new_mirror(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_make_new_mirror(
  context& __ctx, uint64_t destination_server, prx::SSRankMakeNewMirrorReq &request_body, prx::SSRankMakeNewMirrorRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_make_new_mirror(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}


// ============ prx.RanksvrService.rank_check_mirror_dump_finish ============
namespace packer {
RANK_SDK_API bool pack_rank_check_mirror_dump_finish(std::string& output, const prx::SSRankCheckMirrorDumpFinishReq& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_check_mirror_dump_finish",
             __to_string_view(prx::SSRankCheckMirrorDumpFinishReq::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_check_mirror_dump_finish(const std::string& input, prx::SSRankCheckMirrorDumpFinishReq& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_check_mirror_dump_finish",
             __to_string_view(prx::SSRankCheckMirrorDumpFinishReq::descriptor()->full_name()));
}

RANK_SDK_API bool pack_rank_check_mirror_dump_finish(std::string& output, const prx::SSRankCheckMirrorDumpFinishRsp& input) {
  return prx::err::EN_SUCCESS ==
         __pack_rpc_body(
             input, &output, "prx.RanksvrService.rank_check_mirror_dump_finish",
             __to_string_view(prx::SSRankCheckMirrorDumpFinishRsp::descriptor()->full_name()));
}

RANK_SDK_API bool unpack_rank_check_mirror_dump_finish(const std::string& input, prx::SSRankCheckMirrorDumpFinishRsp& output) {
  return prx::err::EN_SUCCESS ==
         __unpack_rpc_body(
             output, input, "prx.RanksvrService.rank_check_mirror_dump_finish",
             __to_string_view(prx::SSRankCheckMirrorDumpFinishRsp::descriptor()->full_name()));
}

}  // namespace packer
namespace unicast {
template<class TargetServerNode>
static rpc::result_code_type __rank_check_mirror_dump_finish(
  context& __ctx, TargetServerNode&& destination_server, prx::SSRankCheckMirrorDumpFinishReq &request_body, prx::SSRankCheckMirrorDumpFinishRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  if (__is_invalid_server_node(destination_server)) {
    RPC_RETURN_CODE(prx::err::EN_SYS_PARAM);
  }

  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "prx.RanksvrService.rank_check_mirror_dump_finish")

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "prx.RanksvrService.rank_check_mirror_dump_finish");
    RPC_RETURN_CODE(prx::err::EN_SYS_MALLOC);
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
  if (__no_wait) {
    res = __setup_rpc_stream_header(
      *req_msg.mutable_head(), "prx.RanksvrService.rank_check_mirror_dump_finish",
    __to_string_view(  prx::SSRankCheckMirrorDumpFinishReq::descriptor()->full_name())
    );
  } else {
    res = __setup_rpc_request_header(
      *req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "prx.RanksvrService.rank_check_mirror_dump_finish",
    __to_string_view(  prx::SSRankCheckMirrorDumpFinishReq::descriptor()->full_name())
    );
  }
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  res = __pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "prx.RanksvrService.rank_check_mirror_dump_finish",
    __to_string_view(prx::SSRankCheckMirrorDumpFinishReq::descriptor()->full_name()));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystem, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcService, "prx.RanksvrService"},
    {opentelemetry::semconv::rpc::kRpcMethod, "prx.RanksvrService.rank_check_mirror_dump_finish"}
  };
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "prx.RanksvrService.rank_check_mirror_dump_finish",
                          __trace_attributes);

  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = prx::RanksvrService::descriptor()
        ->FindMethodByName("rank_check_mirror_dump_finish");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_server_cfg().task().csmsg().timeout(),
                                                              std::chrono::seconds{6});
      }
    }
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(__ctx, response_body,
        "prx.RanksvrService.rank_check_mirror_dump_finish",
        __to_string_view(prx::SSRankCheckMirrorDumpFinishRsp::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "prx.RanksvrService.rank_check_mirror_dump_finish",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  RPC_RETURN_CODE(__tracer.finish({res , __trace_attributes}));
}

RANK_SDK_API rpc::result_code_type rank_check_mirror_dump_finish(
  context& __ctx, const atfw::atapp::etcd_discovery_node& destination_server, prx::SSRankCheckMirrorDumpFinishReq &request_body, prx::SSRankCheckMirrorDumpFinishRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__rank_check_mirror_dump_finish(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}
}  // namespace unicast

RANK_SDK_API rpc::result_code_type rank_check_mirror_dump_finish(
  context& __ctx, uint64_t destination_server, prx::SSRankCheckMirrorDumpFinishReq &request_body, prx::SSRankCheckMirrorDumpFinishRsp &response_body, bool __no_wait, dispatcher_await_options* __wait_later) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__rank_check_mirror_dump_finish(__ctx, destination_server, request_body, response_body, __no_wait, __wait_later)));
}

}  // namespace rank
}
