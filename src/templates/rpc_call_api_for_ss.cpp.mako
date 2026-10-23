## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
output_render_dir = os.path.dirname(output_render_path)
cpp_include_base_dir = ''
if output_render_dir and not os.path.isabs(output_render_dir):
    cpp_include_base_dir = output_render_dir + '/'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${cpp_include_base_dir}${service.get_name_lower_rule()}.atfw.gen.h"

#include <rpc/internal/rpc_template_ss_message.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif
// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility>

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
    rpc_unicast_params_decl = ['context& __ctx']
    rpc_unicast_params_forward = ['__ctx']
    rpc_broadcast_params_decl = ['context& __ctx']
    if rpc_is_router_api:
        rpc_unicast_params_decl.extend(['uint32_t type_id', 'uint32_t zone_id', 'uint64_t object_id'])
        rpc_unicast_params_forward.extend(['type_id', 'zone_id', 'object_id'])
    else:
        rpc_unicast_params_decl.append('TargetServerNode&& destination_server')
        rpc_unicast_params_forward.append('destination_server')
        if rpc_is_user_rpc:
            rpc_unicast_params_decl.extend(['uint32_t zone_id', 'uint64_t user_id'])
            rpc_unicast_params_forward.extend(['zone_id', 'user_id'])
    if rpc_is_user_rpc:
        rpc_broadcast_params_decl.extend(['uint32_t zone_id', 'uint64_t user_id'])
    rpc_unicast_params_decl.append('{0} &request_body'.format(rpc.get_request().get_cpp_class_name()))
    rpc_unicast_params_forward.append('request_body')
    rpc_broadcast_params_decl.append('{0} &request_body'.format(rpc.get_request().get_cpp_class_name()))
    rpc_nowait_params_decl = [x for x in rpc_unicast_params_decl]
    if not rpc_is_stream_mode:
        rpc_unicast_params_decl.append('{0} &response_body'.format(rpc.get_response().get_cpp_class_name()))
        rpc_unicast_params_forward.append('response_body')
        if rpc_allow_no_wait:
            rpc_unicast_params_decl.extend(['bool __no_wait', 'dispatcher_await_options* __wait_later'])
            rpc_unicast_params_forward.extend(['__no_wait', '__wait_later'])
    if rpc_allow_ignore_discovery:
      rpc_unicast_params_decl.append('bool __ignore_discovery')
      rpc_unicast_params_forward.append('__ignore_discovery')
      rpc_nowait_params_decl.append('bool __ignore_discovery')
    if not rpc_is_router_api and rpc_is_stream_mode:
        rpc_return_type = 'rpc::always_ready_code_type'
    else:
        rpc_return_type = 'rpc::result_code_type'
    rpc_unicast_params_decl_modern = []
    rpc_unicast_params_decl_legacy = []
    for param in rpc_unicast_params_decl:
        if 'TargetServerNode&& destination_server' == param:
            rpc_unicast_params_decl_modern.append('const atfw::atapp::etcd_discovery_node& destination_server')
            rpc_unicast_params_decl_legacy.append('uint64_t destination_server')
        else:
            rpc_unicast_params_decl_modern.append(param)
            rpc_unicast_params_decl_legacy.append(param)
    rpc_nowait_params_decl_modern = []
    for param in rpc_nowait_params_decl:
        if 'TargetServerNode&& destination_server' == param:
            rpc_nowait_params_decl_modern.append('const atfw::atapp::etcd_discovery_node& destination_server')
        else:
            rpc_nowait_params_decl_modern.append(param)
%>
// ============ ${rpc.get_service().get_full_name()}/${rpc.get_name()} ============
namespace {
static internal::ss_rpc_options options_for_${rpc.get_name()}(
    uint32_t zone_id, uint64_t user_id, ATFW_EXPLICIT_UNUSED_ATTR bool redirect_info = ${'true' if rpc_is_stream_mode else 'false'}) {
  internal::ss_rpc_options options;
  options.service_full_name = "${service.get_full_name()}";
  options.rpc_full_name = "${rpc.get_service().get_full_name()}/${rpc.get_name()}";
  options.response_type_full_name = internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());
  options.method = ${service.get_cpp_class_name()}::descriptor()->FindMethodByName("${rpc.get_name()}");
%   for level in ['warning', 'info']:
<% codes = rpc.get_extension_field('atframework.rpc_options', lambda x: getattr(x, level + '_log_response_code'), []) %>
%     if codes:
  static const int ${level}_codes[] = {${', '.join(str(code) for code in codes)}};
%       if level == 'info':
  // Preserve the existing log levels: only broadcast and regular stream calls redirect info codes.
  if (redirect_info) {
    options.info_codes = info_codes;
  }
%       else:
  options.${level}_codes = ${level}_codes;
%       endif
%     endif
%   endfor
  options.user_rpc = ${'true' if rpc_is_user_rpc else 'false'};
  options.zone_id = zone_id;
  options.user_id = user_id;
  return options;
}
}  // namespace

namespace packer {
${rpc_dllexport_decl} gsl::string_view get_full_name_of_${rpc.get_name()}() {
  return "${rpc.get_service().get_full_name()}/${rpc.get_name()}";
}

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
<%
    rpc_user_args = 'zone_id, ' + ('object_id' if rpc_is_router_api else 'user_id') if rpc_is_user_rpc else '0, 0'
    rpc_broadcast_user_args = 'zone_id, user_id' if rpc_is_user_rpc else '0, 0'
    rpc_target = 'internal::ss_rpc_router_target{router_object_base::key_t{type_id, zone_id, object_id}}' if rpc_is_router_api else ('internal::ss_rpc_unicast_target<TargetServerNode>{&destination_server, ' + ('__ignore_discovery' if rpc_allow_ignore_discovery else 'false') + '}')
%>
% if rpc.get_extension_field('atframework.rpc_options', lambda x: x.enable_broadcast, False):
namespace broadcast {
${rpc_dllexport_decl} rpc::always_ready_code_type ${rpc.get_name()}(
    ${', '.join(rpc_broadcast_params_decl)},
    const ss_msg_logic_index& index, ::atfw::atapp::protocol::atapp_metadata *metadata) {
  return {internal::send_ss_rpc(__ctx, request_body, options_for_${rpc.get_name()}(${rpc_broadcast_user_args}, true),
                               internal::ss_rpc_broadcast_target{&index, metadata}),};
}
}  // namespace broadcast
% endif
% if (rpc_is_stream_mode or rpc_allow_no_wait) and not rpc_is_router_api:
namespace oneway {
${rpc_dllexport_decl} ${rpc_return_type}::value_type ${rpc.get_name()}(
    ${', '.join(rpc_nowait_params_decl_modern)}) {
  return internal::send_ss_rpc(__ctx, request_body, options_for_${rpc.get_name()}(${rpc_user_args}, false),
      internal::ss_rpc_unicast_target<atfw::atapp::etcd_discovery_node>{&destination_server, ${'__ignore_discovery' if rpc_allow_ignore_discovery else 'false'}});
}
}  // namespace oneway
% endif
// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-missing-std-forward)
namespace unicast {
% if not rpc_is_router_api:
template<class TargetServerNode>
% endif
static ${rpc_return_type} __${rpc.get_name()}(
    ${', '.join(rpc_unicast_params_decl_modern if rpc_is_router_api else rpc_unicast_params_decl)}) {
% if not rpc_is_router_api and rpc_is_stream_mode:
  return {internal::send_ss_rpc(__ctx, request_body, options_for_${rpc.get_name()}(${rpc_user_args}),
                               ${rpc_target}),};
% else:
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(internal::call_ss_rpc(
      __ctx, request_body, ${'static_cast<' + rpc.get_response().get_cpp_class_name() + '*>(nullptr)' if rpc_is_stream_mode else '&response_body'},
      options_for_${rpc.get_name()}(${rpc_user_args}), ${rpc_target}${', __no_wait, __wait_later' if rpc_allow_no_wait else ''})));
% endif
}

${rpc_dllexport_decl} ${rpc_return_type} ${rpc.get_name()}(
    ${', '.join(rpc_unicast_params_decl_modern)}) {
% if not rpc_is_router_api and rpc_is_stream_mode:
  return __${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)});
% else:
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(__${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)})));
% endif
}
}  // namespace unicast
// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-missing-std-forward)

${rpc_dllexport_decl} ${rpc_return_type} ${rpc.get_name()}(
    ${', '.join(rpc_unicast_params_decl_legacy)}) {
% if not rpc_is_router_api and rpc_is_stream_mode:
  return unicast::__${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)});
% else:
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(unicast::__${rpc.get_name()}(${', '.join(rpc_unicast_params_forward)})));
% endif
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
  return internal::register_ss_rpc_mock<${rpc.get_request().get_cpp_class_name()}, ${rpc.get_response().get_cpp_class_name()}>(
      packer::get_full_name_of_${rpc.get_name()}(), std::move(__handler), __options);
}
% endfor
}  // namespace mock
#endif
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}
