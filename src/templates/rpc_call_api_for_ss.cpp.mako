## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
def rpc_return_result_code_sentense(input):
  return 'RPC_RETURN_CODE({});'.format(input)

def rpc_return_always_ready_code_sentense(input):
  return 'return {static_cast<rpc::always_ready_code_type::value_type>(' + input + ')};'

%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${service.get_name_lower_rule()}.h"

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
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

// clang-format offc
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/trace/semantic_conventions.h>

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
<%
rpc_common_codes_enable_redirect_info_log = False
rpc_common_codes_enable_redirect_warning_log = False
rpc_common_codes_enable_wait_response = False
rpc_common_codes_enable_stream_header = False
rpc_common_codes_enable_request_header = False
rpc_common_codes_has_no_router_rpc = False
rpc_common_codes_enable_common = len(rpcs.values()) > 0

for rpc in rpcs.values():
    if not rpc_common_codes_has_no_router_rpc and not rpc.get_extension_field('atframework.rpc_options', lambda x: x.router_rpc, False):
        rpc_common_codes_has_no_router_rpc = True
    if not rpc_common_codes_enable_wait_response and not rpc.is_request_stream() and not rpc.is_response_stream():
        rpc_common_codes_enable_wait_response = True

    if not rpc_common_codes_enable_redirect_warning_log and rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []):
        rpc_common_codes_enable_redirect_warning_log = True

    if not rpc_common_codes_enable_redirect_info_log and rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []):
        rpc_common_codes_enable_redirect_info_log = True

    if not rpc_common_codes_enable_stream_header or not rpc_common_codes_enable_request_header:
        rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
        rpc_allow_no_wait = False
        rpc_enable_broadcast = rpc.get_extension_field('atframework.rpc_options', lambda x: x.enable_broadcast, False)
        if not rpc_is_stream_mode:
            rpc_allow_no_wait = rpc.get_extension_field('atframework.rpc_options', lambda x: x.allow_no_wait, False)
        if rpc_is_stream_mode or rpc_allow_no_wait or rpc_enable_broadcast:
            rpc_common_codes_enable_stream_header = True
        if not rpc_is_stream_mode:
            rpc_common_codes_enable_request_header = True
%>namespace details {
% if rpc_common_codes_enable_common:
%   if rpc_common_codes_has_no_router_rpc:
UTIL_FORCEINLINE static bool __is_invalid_server_node(const atapp::etcd_discovery_node& destination_server) {
  return destination_server.get_discovery_info().id() == 0 || destination_server.get_discovery_info().name().empty();
}

UTIL_FORCEINLINE static bool __is_invalid_server_node(uint64_t destination_server) {
  return destination_server == 0;
}
%   endif

template<class TBodyType>
inline static int __pack_rpc_body(TBodyType &&input, std::string *output, gsl::string_view rpc_full_name,
                                const std::string &type_full_name) {
  if (false == input.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
              input.InitializationErrorString());
    return ${project_namespace}::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
              protobuf_mini_dumper_get_readable(input));
    return ${project_namespace}::err::EN_SUCCESS;
  }
}

template<class TBodyType>
inline static int __unpack_rpc_body(TBodyType &&output, const std::string& input,
                                gsl::string_view rpc_full_name,
                                const std::string &type_full_name) {
  if (false == output.ParseFromString(input)) {
    FWLOGERROR("rpc {} parse message {} failed, msg: {}", rpc_full_name, type_full_name,
              output.InitializationErrorString());
    return ${project_namespace}::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name,
              protobuf_mini_dumper_get_readable(output));
    return ${project_namespace}::err::EN_SUCCESS;
  }
}

inline static rpc::context::tracer::span_ptr_type __setup_tracer(rpc::context &__child_ctx,
                                  rpc::context::tracer &__tracer, atframework::SSMsgHead &head,
                                  gsl::string_view rpc_full_name,
                                  rpc::telemetry::trace_attributes_type attributes) {
  rpc::context::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = attributes;

  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/semantic_conventions/README.md
  __child_ctx.setup_tracer(__tracer, rpc::context::string_view{rpc_full_name.data(), rpc_full_name.size()},
    std::move(__trace_option));
  if (__tracer.is_recording()) {
    rpc::context::tracer::span_ptr_type __child_trace_span = __child_ctx.get_trace_span();
    if (__child_trace_span) {
      auto trace_span_head = head.mutable_rpc_trace();
      if (trace_span_head) {
        auto trace_context = __child_trace_span->GetContext();
        rpc::context::tracer::trace_id_span trace_id = trace_context.trace_id().Id();
        rpc::context::tracer::span_id_span span_id = trace_context.span_id().Id();

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

    return rpc::context::tracer::span_ptr_type();
  }
}

% endif
% if rpc_common_codes_enable_stream_header:
inline static int __setup_rpc_stream_header(atframework::SSMsgHead &head, gsl::string_view rpc_full_name,
                                            const std::string &type_full_name) {
  head.set_op_type(${project_namespace}::EN_MSG_OP_TYPE_STREAM);
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee("${service.get_full_name()}");
  stream_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  stream_meta->set_type_url(type_full_name);
  stream_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  stream_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return ${project_namespace}::err::EN_SUCCESS;
}
% endif
% if rpc_common_codes_enable_request_header:
inline static int __setup_rpc_request_header(atframework::SSMsgHead &head, task_type_trait::id_type task_id,
                                             gsl::string_view rpc_full_name, const std::string &type_full_name) {
  head.set_source_task_id(task_id);
  head.set_op_type(${project_namespace}::EN_MSG_OP_TYPE_UNARY_REQUEST);
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }
  request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  request_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  request_meta->set_callee("${service.get_full_name()}");
  request_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  request_meta->set_type_url(type_full_name);
  request_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  request_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return ${project_namespace}::err::EN_SUCCESS;
}
% endif
% if rpc_common_codes_enable_redirect_info_log:
template<class TCode, class TConvertList>
inline static bool __redirect_rpc_result_to_info_log(TCode &origin_result, TConvertList&& convert_list,
                                        gsl::string_view rpc_full_name, const std::string &type_full_name) {
  for (auto& check: convert_list) {
    if (origin_result == check) {
      FWLOGINFO("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name,
                origin_result, protobuf_mini_dumper_get_error_msg(origin_result)
      );

      return true;
    }
  }

  return false;
}
% endif
% if rpc_common_codes_enable_redirect_warning_log:
template<class TCode, class TConvertList>
inline static bool __redirect_rpc_result_to_warning_log(TCode &origin_result, TConvertList&& convert_list,
                                        gsl::string_view rpc_full_name, const std::string &type_full_name) {
  for (auto& check: convert_list) {
    if (origin_result == check) {
      FWLOGWARNING("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name,
                   origin_result, protobuf_mini_dumper_get_error_msg(origin_result)
      );

      return true;
    }
  }

  return false;
}
% endif
% if rpc_common_codes_enable_wait_response:
template<class TResponseBody>
inline static rpc::result_code_type __rpc_wait_and_unpack_response(rpc::context &__ctx, TResponseBody &response_body,
                                            gsl::string_view rpc_full_name, const std::string &type_full_name,
                                            dispatcher_await_options& await_options) {
  atframework::SSMsg* rsp_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == rsp_msg_ptr) {
    FWLOGERROR("rpc {} create response message failed", rpc_full_name);
    RPC_RETURN_CODE(${project_namespace}::err::EN_SYS_MALLOC);
  }

  atframework::SSMsg& rsp_msg = *rsp_msg_ptr;
  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(rpc::wait(__ctx, rsp_msg, await_options));

  if (rsp_msg.head().rpc_response().type_url() != type_full_name) {
    if (res >= 0 || !rsp_msg.head().rpc_response().type_url().empty()) {
      FWLOGERROR("rpc {} expect response message {}, but got {}", rpc_full_name, type_full_name,
                 rsp_msg.head().rpc_response().type_url());
    }
  } else if (!rsp_msg.body_bin().empty()) {
    res = details::__unpack_rpc_body(response_body, rsp_msg.body_bin(), rpc_full_name, type_full_name);
    if(res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  if (rsp_msg.has_head() && rsp_msg.head().error_code() != 0) {
    RPC_RETURN_CODE(rsp_msg.head().error_code());
  }
  RPC_RETURN_CODE(res);
}
% endif
}  // namespace details

% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    rpc_is_router_api = rpc.get_extension_field('atframework.rpc_options', lambda x: x.router_rpc, False)
    rpc_is_user_rpc = rpc.get_extension_field('atframework.rpc_options', lambda x: x.user_rpc, False)
    rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
    rpc_allow_ignore_discovery = rpc.get_extension_field('atframework.rpc_options', lambda x: x.allow_ignore_discovery, False)
    rpc_allow_no_wait = False
    if not rpc_is_stream_mode:
        rpc_allow_no_wait = rpc.get_extension_field('atframework.rpc_options', lambda x: x.allow_no_wait, False)
    if rpc_allow_no_wait:
        rpc_request_meta_pretty_prefix = '  '
    else:
        rpc_request_meta_pretty_prefix = ''
    rpc_unicast_params_decl = ['context& __ctx']
    rpc_unicast_params_forward = ['__ctx']
    rpc_broadcast_params_decl = ['context& __ctx']
    rpc_broadcast_params_forward = ['__ctx']
    if rpc_is_router_api:
        rpc_unicast_params_decl.extend(['uint32_t type_id', 'uint32_t zone_id', 'uint64_t object_id'])
        rpc_unicast_params_forward.extend(['type_id', 'zone_id', 'object_id'])
    else:
        rpc_unicast_params_decl.append('TargetServerNode&& destination_server')
        rpc_unicast_params_forward.append('destination_server')
        if rpc_is_user_rpc:
            rpc_unicast_params_decl.extend(['uint32_t zone_id', 'uint64_t user_id', "const std::string& open_id"])
            rpc_unicast_params_forward.extend(['zone_id', 'user_id', "open_id"])
    if rpc_is_user_rpc:
        rpc_broadcast_params_decl.extend(['uint32_t zone_id', 'uint64_t user_id', "const std::string& open_id"])
        rpc_broadcast_params_forward.extend(['zone_id', 'user_id', "open_id"])
    rpc_unicast_params_decl.append('{0} &request_body'.format(rpc.get_request().get_cpp_class_name()))
    rpc_unicast_params_forward.append('request_body')
    rpc_broadcast_params_decl.append('{0} &request_body'.format(rpc.get_request().get_cpp_class_name()))
    rpc_broadcast_params_forward.append('request_body')
    if not rpc_is_stream_mode:
        rpc_unicast_params_decl.append('{0} &response_body'.format(rpc.get_response().get_cpp_class_name()))
        rpc_unicast_params_forward.append('response_body')
        if rpc_allow_no_wait:
            rpc_unicast_params_decl.extend(['bool __no_wait', 'dispatcher_await_options* __wait_later'])
            rpc_unicast_params_forward.extend(['__no_wait', '__wait_later'])
    if rpc_allow_ignore_discovery:
      rpc_unicast_params_decl.append('bool __ignore_discovery')
      rpc_unicast_params_forward.append('__ignore_discovery')
    if not rpc_is_router_api and rpc_is_stream_mode:
        rpc_return_type = 'rpc::always_ready_code_type'
        rpc_return_sentense = rpc_return_always_ready_code_sentense
    else:
        rpc_return_type = 'rpc::result_code_type'
        rpc_return_sentense = rpc_return_result_code_sentense
    rpc_unicast_params_decl_modern = []
    rpc_unicast_params_decl_legacy = []
    for param in rpc_unicast_params_decl:
        if 'TargetServerNode&& destination_server' == param:
            rpc_unicast_params_decl_modern.append('const atapp::etcd_discovery_node& destination_server')
            rpc_unicast_params_decl_legacy.append('uint64_t destination_server')
        else:
            rpc_unicast_params_decl_modern.append(param)
            rpc_unicast_params_decl_legacy.append(param)
%>
// ============ ${rpc.get_full_name()} ============
namespace packer {
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string& output, const ${rpc.get_request().get_cpp_class_name()}& input) {
  return ${project_namespace}::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "${rpc.get_full_name()}",
                                 ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
}

${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string& input, ${rpc.get_request().get_cpp_class_name()}& output) {
  return ${project_namespace}::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "${rpc.get_full_name()}",
                                 ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
}

% if not rpc_is_stream_mode:
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string& output, const ${rpc.get_response().get_cpp_class_name()}& input) {
  return ${project_namespace}::err::EN_SUCCESS == details::__pack_rpc_body(input, &output,
                                 "${rpc.get_full_name()}",
                                 ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());
}

${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string& input, ${rpc.get_response().get_cpp_class_name()}& output) {
  return ${project_namespace}::err::EN_SUCCESS == details::__unpack_rpc_body(output, input,
                                 "${rpc.get_full_name()}",
                                 ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());
}

% endif
}  // namespace packer
% if rpc.get_extension_field('atframework.rpc_options', lambda x: x.enable_broadcast, False):
namespace broadcast {
${rpc_dllexport_decl} ${rpc_return_type} ${rpc.get_name()}(
    ${', '.join(rpc_broadcast_params_decl)},
    const ss_msg_logic_index& index, ::atapp::protocol::atapp_metadata *metadata) {
  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "${rpc.get_full_name()}");
    ${rpc_return_always_ready_code_sentense(project_namespace + '::err::EN_SYS_MALLOC')}
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());

  ${rpc_request_meta_pretty_prefix}res = details::__setup_rpc_stream_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head(), "${rpc.get_full_name()}",
    ${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()
  ${rpc_request_meta_pretty_prefix});

  if (res < 0) {
    ${rpc_return_always_ready_code_sentense('res')}
  }

  res = details::__pack_rpc_body(request_body, req_msg.mutable_body_bin(),
                                 "${rpc.get_full_name()}",
                                 ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
  if (res < 0) {
    ${rpc_return_always_ready_code_sentense('res')}
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.ss"},
    {opentelemetry::trace::SemanticConventions::kRpcService, "${rpc.get_service().get_full_name()}"},
    {opentelemetry::trace::SemanticConventions::kRpcMethod, "${rpc.get_full_name()}"}
  };
%   if rpc_is_user_rpc:
  auto __child_trace_span = details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   else:
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   endif
                          "${rpc.get_full_name()}",
                          __trace_attributes);

%   if rpc_is_user_rpc:
  req_msg.mutable_head()->set_player_user_id(user_id);
  req_msg.mutable_head()->set_player_zone_id(zone_id);
  req_msg.mutable_head()->set_player_open_id(open_id);
  if (__child_trace_span) {
    __child_trace_span->SetAttribute("user_id", user_id);
    __child_trace_span->SetAttribute("zone_id", zone_id);
  }
%   endif

  res = ss_msg_dispatcher::me()->broadcast(req_msg, index, metadata);

  if (res < 0) {
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []):
    const int warning_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []))}};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
        ${rpc_return_always_ready_code_sentense('__tracer.finish({res, __trace_attributes})')}
  }
%     endif
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []):
  const int info_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []))}};
  if (details::__redirect_rpc_result_to_info_log(res, info_codes,
      "${rpc.get_full_name()}",
      ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
    ${rpc_return_always_ready_code_sentense('__tracer.finish({res, __trace_attributes})')}
  }
%     endif
    FWLOGERROR("rpc {} call failed, res: {}({})",
               "${rpc.get_full_name()}",
               res, protobuf_mini_dumper_get_error_msg(res)
    );
  }
  ${rpc_return_always_ready_code_sentense('__tracer.finish({res, __trace_attributes})')}
}
}  // namespace broadcast
% endif
namespace unicast {
%   if rpc_is_router_api:
static ${rpc_return_type} __${rpc.get_name()}(
  ${', '.join(rpc_unicast_params_decl_modern)}) {
%   else:
template<class TargetServerNode>
static ${rpc_return_type} __${rpc.get_name()}(
  ${', '.join(rpc_unicast_params_decl)}) {
%   endif
%   if rpc_is_router_api:
  if (object_id == 0 || type_id == 0) {
    ${rpc_return_sentense(project_namespace + '::err::EN_SYS_PARAM')}
  }
%   else:
  if (details::__is_invalid_server_node(destination_server)) {
    ${rpc_return_sentense(project_namespace + '::err::EN_SYS_PARAM')}
  }
%   endif

%   if rpc_is_router_api or not rpc_is_stream_mode:
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "${rpc.get_full_name()}")
%   endif

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "${rpc.get_full_name()}");
    ${rpc_return_sentense(project_namespace + '::err::EN_SYS_MALLOC')}
  }

  rpc::result_code_type::value_type res;
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
%   if rpc_allow_no_wait:
  if (__no_wait) {
%   endif
%   if rpc_is_stream_mode or rpc_allow_no_wait:
  ${rpc_request_meta_pretty_prefix}res = details::__setup_rpc_stream_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head(), "${rpc.get_full_name()}",
    ${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()
  ${rpc_request_meta_pretty_prefix});
%   endif
%   if rpc_allow_no_wait:
  } else {
%   endif
%   if not rpc_is_stream_mode:
  ${rpc_request_meta_pretty_prefix}res = details::__setup_rpc_request_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "${rpc.get_full_name()}",
    ${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()
  ${rpc_request_meta_pretty_prefix});
%   endif
%   if rpc_allow_no_wait:
  }
%   endif
  if (res < 0) {
    ${rpc_return_sentense('res')}
  }

  res = details::__pack_rpc_body(request_body, req_msg.mutable_body_bin(),
                                 "${rpc.get_full_name()}",
                                 ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
  if (res < 0) {
    ${rpc_return_sentense('res')}
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::trace::SemanticConventions::kRpcSystem, "atrpc.ss"},
    {opentelemetry::trace::SemanticConventions::kRpcService, "${rpc.get_service().get_full_name()}"},
    {opentelemetry::trace::SemanticConventions::kRpcMethod, "${rpc.get_full_name()}"}
  };
%   if rpc_is_user_rpc or rpc_is_router_api:
  auto __child_trace_span = details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   else:
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   endif
                          "${rpc.get_full_name()}",
                          __trace_attributes);

%   if rpc_is_user_rpc:
%     if rpc_is_router_api:
  req_msg.mutable_head()->set_player_user_id(object_id);
  req_msg.mutable_head()->set_player_zone_id(zone_id);
  if (__child_trace_span) {
    __child_trace_span->SetAttribute("user_id", object_id);
    __child_trace_span->SetAttribute("zone_id", zone_id);
  }
%     else:
  req_msg.mutable_head()->set_player_user_id(user_id);
  req_msg.mutable_head()->set_player_zone_id(zone_id);
  req_msg.mutable_head()->set_player_open_id(open_id);
  if (__child_trace_span) {
    __child_trace_span->SetAttribute("user_id", user_id);
    __child_trace_span->SetAttribute("zone_id", zone_id);
  }
%     endif
%   endif
%   if rpc_is_router_api:
%     if not rpc_is_user_rpc:
  if (type_id == router_player_manager::me()->get_type_id()) {
    req_msg.mutable_head()->set_player_user_id(object_id);
    req_msg.mutable_head()->set_player_zone_id(zone_id);
  }
  if (__child_trace_span) {
    __child_trace_span->SetAttribute("router_object.type_id", type_id);
    __child_trace_span->SetAttribute("router_object.zone_id", zone_id);
    __child_trace_span->SetAttribute("router_object.instance_id", object_id);
  }
%     endif

  router_object_base::key_t router_key(type_id, zone_id, object_id);
  router_object_base::trace_router(__child_ctx, router_key);

  router_manager_base* router_manager = router_manager_set::me()->get_manager(type_id);
  if (nullptr == router_manager) {
    FWLOGERROR("rpc {} can not get router manager of type {}",
               "${rpc.get_full_name()}", type_id);
    RPC_RETURN_CODE(__tracer.finish({${project_namespace}::err::EN_SYS_NOT_SUPPORT, __trace_attributes}));
  }

  uint64_t rpc_sequence = 0;
  res = RPC_AWAIT_CODE_RESULT(router_manager->send_msg(__ctx, router_key, std::move(req_msg), rpc_sequence));
%   else:
%     if rpc_allow_ignore_discovery:
  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg, __ignore_discovery);
%     else:
  res = ss_msg_dispatcher::me()->send_to_proc(destination_server, req_msg);
%     endif
%   endif
%   if rpc_is_stream_mode:
%     if rpc_is_router_api:
  if (res == PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND || res == PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_IN_SERVER) {
    FWLOGWARNING("rpc {} call ignored for router cache {},{},{}: {}({})",
               "${rpc.get_full_name()}",
               router_key.type_id, router_key.zone_id, router_key.object_id,
               res, protobuf_mini_dumper_get_error_msg(res)
    );
  } else if (res < 0) {
%     else:
  if (res < 0) {
%     endif
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []):
    const int warning_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []))}};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
        ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
  }
%     endif
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []):
  const int info_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []))}};
  if (details::__redirect_rpc_result_to_info_log(res, info_codes,
      "${rpc.get_full_name()}",
      ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
    ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
  }
%     endif
    FWLOGERROR("rpc {} call failed, res: {}({})",
               "${rpc.get_full_name()}",
               res, protobuf_mini_dumper_get_error_msg(res)
    );
  }
  ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
%   else:
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    {
      const google::protobuf::MethodDescriptor *method = ${service.get_cpp_class_name()}::descriptor()
        ->FindMethodByName("${rpc.get_name()}");

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
%     if rpc_allow_no_wait:
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = await_options;
      // need to call RPC_AWAIT_CODE_RESULT(rpc::wait(...)) to wait this rpc sequence later
      break;
    }
%     endif
    if (res < 0) {
      break;
    }
    res = RPC_AWAIT_CODE_RESULT(details::__rpc_wait_and_unpack_response(__ctx, response_body,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), await_options));
  } while (false);

  if (res < 0) {
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []):
    const int warning_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []))}};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
      ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
    }
%     endif
%     if warning_log_codes in rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []):
    const int info_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []))}};
    if (details::__redirect_rpc_result_to_info_log(res, info_codes,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
      ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
    }
%     endif
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "${rpc.get_full_name()}",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
%   endif
}

${rpc_dllexport_decl} ${rpc_return_type} ${rpc.get_name()}(
  ${', '.join(rpc_unicast_params_decl_modern)}) {
%   if not rpc_is_router_api and rpc_is_stream_mode:
  return __${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)});
%   else:
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)})));
%   endif
}
}  // namespace unicast

${rpc_dllexport_decl} ${rpc_return_type} ${rpc.get_name()}(
  ${', '.join(rpc_unicast_params_decl_legacy)}) {
%   if not rpc_is_router_api and rpc_is_stream_mode:
  return unicast::__${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)});
%   else:
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)})));
%   endif
}

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}
