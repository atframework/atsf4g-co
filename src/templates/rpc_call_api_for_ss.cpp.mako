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

output_render_dir = os.path.dirname(output_render_path)
cpp_include_base_dir = ''
if output_render_dir and not os.path.isabs(output_render_dir):
    cpp_include_base_dir = output_render_dir + '/'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${cpp_include_base_dir}${service.get_name_lower_rule()}.atfw.gen.h"

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>

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
#include <rpc/internal/rpc_template_ss_message.h>

#include <cstdint> // IWYU pragma: keep
#include <string>  // IWYU pragma: keep

namespace rpc {
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
            rpc_unicast_params_decl_modern.append('const atfw::atapp::etcd_discovery_node& destination_server')
            rpc_unicast_params_decl_legacy.append('uint64_t destination_server')
        else:
            rpc_unicast_params_decl_modern.append(param)
            rpc_unicast_params_decl_legacy.append(param)
%>
// ============ ${rpc.get_service().get_full_name()}/${rpc.get_name()} ============
namespace packer {
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string& output, const ${rpc.get_request().get_cpp_class_name()}& input) {
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS ==
         internal::pack_rpc_body(
             input, &output, "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
             internal::to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()));
}

${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string& input, ${rpc.get_request().get_cpp_class_name()}& output) {
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS ==
         internal::unpack_rpc_body(
             output, input, "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
             internal::to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()));
}

% if not rpc_is_stream_mode:
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string& output, const ${rpc.get_response().get_cpp_class_name()}& input) {
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS ==
         internal::pack_rpc_body(
             input, &output, "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
             internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
}

${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string& input, ${rpc.get_response().get_cpp_class_name()}& output) {
  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS ==
         internal::unpack_rpc_body(
             output, input, "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
             internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
}

% endif
}  // namespace packer
% if rpc.get_extension_field('atframework.rpc_options', lambda x: x.enable_broadcast, False):
namespace broadcast {
// Broadcast is fire and forget, so it always returns rpc::always_ready_code_type even for non-stream RPCs.
${rpc_dllexport_decl} rpc::always_ready_code_type ${rpc.get_name()}(
    ${', '.join(rpc_broadcast_params_decl)},
    const ss_msg_logic_index& index, ::atfw::atapp::protocol::atapp_metadata *metadata) {
  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}");
    ${rpc_return_always_ready_code_sentense(project_namespace + '::err::EN_SYS_MALLOC')}
  }

  rpc::result_code_type::value_type res{};
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());

  ${rpc_request_meta_pretty_prefix}res = rpc::setup_rpc_stream_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head()->mutable_rpc_stream(), "${service.get_full_name()}", "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name())
  ${rpc_request_meta_pretty_prefix});

  if (res < 0) {
    ${rpc_return_always_ready_code_sentense('res')}
  }

  res = internal::pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    ${rpc_return_always_ready_code_sentense('res')}
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcMethod, "${rpc.get_service().get_full_name()}/${rpc.get_name()}"}
  };
%   if rpc_is_user_rpc:
  auto __child_trace_span = rpc::setup_rpc_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   else:
  rpc::setup_rpc_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   endif
                          "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
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
    if (internal::redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
        internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()))) {
        ${rpc_return_always_ready_code_sentense('__tracer.finish({res, __trace_attributes})')}
  }
%     endif
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []):
  const int info_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []))}};
  if (internal::redirect_rpc_result_to_info_log(res, info_codes,
      "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
      internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()))) {
    ${rpc_return_always_ready_code_sentense('__tracer.finish({res, __trace_attributes})')}
  }
%     endif
    FWLOGERROR("rpc {} call failed, res: {}({})",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
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
  if (internal::is_invalid_server_node(destination_server)) {
    ${rpc_return_sentense(project_namespace + '::err::EN_SYS_PARAM')}
  }
%   endif

%   if rpc_is_router_api or not rpc_is_stream_mode:
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("rpc {} must be called in a task",
    "${rpc.get_service().get_full_name()}/${rpc.get_name()}")
%   endif

  atframework::SSMsg* req_msg_ptr = __ctx.create<atframework::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}");
    ${rpc_return_sentense(project_namespace + '::err::EN_SYS_MALLOC')}
  }

  rpc::result_code_type::value_type res{};
  atframework::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id(),
    logic_config::me()->get_local_server_name());
%   if rpc_allow_no_wait:
  if (__no_wait) {
%   endif
%   if rpc_is_stream_mode or rpc_allow_no_wait:
  ${rpc_request_meta_pretty_prefix}res = rpc::setup_rpc_stream_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head()->mutable_rpc_stream(), "${service.get_full_name()}","${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name())
  ${rpc_request_meta_pretty_prefix});
%   endif
%   if rpc_allow_no_wait:
  } else {
%   endif
%   if not rpc_is_stream_mode:
  ${rpc_request_meta_pretty_prefix}res = internal::setup_ss_rpc_request_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head(), __ctx.get_task_context().task_id,
    "${service.get_full_name()}",
    "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name())
  ${rpc_request_meta_pretty_prefix});
%   endif
%   if rpc_allow_no_wait:
  }
%   endif
  if (res < 0) {
    ${rpc_return_sentense('res')}
  }

  res = internal::pack_rpc_body(
    request_body, req_msg.mutable_body_bin(), "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    ${rpc_return_sentense('res')}
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
    {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.ss"},
    {opentelemetry::semconv::rpc::kRpcMethod, "${rpc.get_service().get_full_name()}/${rpc.get_name()}"}
  };
%   if rpc_is_user_rpc or rpc_is_router_api:
  auto __child_trace_span = rpc::setup_rpc_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   else:
  rpc::setup_rpc_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
%   endif
                          "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
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
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}", type_id);
    RPC_RETURN_CODE(__tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_NOT_SUPPORT, __trace_attributes}));
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
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
               router_key.type_id, router_key.zone_id, router_key.object_id,
               res, protobuf_mini_dumper_get_error_msg(res)
    );
  } else if (res < 0) {
%     else:
  if (res < 0) {
%     endif
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []):
    const int warning_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []))}};
    if (internal::redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
        internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()))) {
        ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
  }
%     endif
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []):
  const int info_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []))}};
  if (internal::redirect_rpc_result_to_info_log(res, info_codes,
      "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
      internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()))) {
    ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
  }
%     endif
    FWLOGERROR("rpc {} call failed, res: {}({})",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
               res, protobuf_mini_dumper_get_error_msg(res)
    );
  }
  ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
%   else:
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
%   if rpc_is_router_api:
    // req_msg is moved into router_manager->send_msg() above, use the returned sequence instead
    await_options.sequence = rpc_sequence;
%   else:
    await_options.sequence = req_msg.head().sequence();
%   endif
    {
      const google::protobuf::MethodDescriptor *method = ${service.get_cpp_class_name()}::descriptor()
        ->FindMethodByName("${rpc.get_name()}");

      if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
        await_options.timeout = rpc::make_duration_or_default(
            method->options().GetExtension(atframework::rpc_options).timeout(),
            rpc::make_duration_or_default(logic_config::me()->get_logic_cfg().task().csmsg().timeout(),
                                          std::chrono::seconds{6}));
      } else {
        await_options.timeout = rpc::make_duration_or_default(logic_config::me()->get_logic_cfg().task().csmsg().timeout(),
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

    res = RPC_AWAIT_CODE_RESULT(internal::wait_and_unpack_ss_response(__ctx, response_body,
        "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
        internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()),
        await_options));
  } while (false);

  if (res < 0) {
%     if rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []):
    const int warning_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.warning_log_response_code, []))}};
    if (internal::redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
        internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()))) {
      ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
    }
%     endif
%     if warning_log_codes in rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []):
    const int info_codes[] = {${', '.join(rpc.get_extension_field('atframework.rpc_options', lambda x: x.info_log_response_code, []))}};
    if (internal::redirect_rpc_result_to_info_log(res, info_codes,
        "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
        internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()))) {
      ${rpc_return_sentense('__tracer.finish({res , __trace_attributes})')}
    }
%     endif
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
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
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
namespace mock {
% for rpc in rpcs.values():
${rpc_dllexport_decl} rpc::unit_test::mock_rule_handle ${rpc.get_name()}(
    std::function<rpc::result_code_type(rpc::context &, const ${rpc.get_request().get_cpp_class_name()} &,
                      ${rpc.get_response().get_cpp_class_name()} &)>
        __handler,
    const rpc::unit_test::ss_mock_rule_options &__options) {
  const auto &__bridge = rpc::unit_test::get_mock_engine_bridge_for_unit_test();
  if (!__bridge.register_ss_rule || !__handler) {
    return rpc::unit_test::mock_rule_handle{};
  }
  return rpc::unit_test::mock_rule_handle{__bridge.register_ss_rule(
      "${service.get_full_name()}/${rpc.get_name()}",
      ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name(),
      ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(),
      [__handler = std::move(__handler)](const rpc::unit_test::ss_mock_request_view &__view,
                                         google::protobuf::Message &__response) -> rpc::result_code_type {
        if (nullptr == __view.body || nullptr == __view.context) {
          RPC_RETURN_CODE(-1);
        }
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__handler(*__view.context,
            static_cast<const ${rpc.get_request().get_cpp_class_name()} &>(*__view.body),
            static_cast<${rpc.get_response().get_cpp_class_name()} &>(__response))));
      },
      __options)};
}
% endfor
}  // namespace mock
#endif
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}
