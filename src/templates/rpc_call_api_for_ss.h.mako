## -*- coding: utf-8 -*-
<%!
import time
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
service_proto_file_path = service.file.get_name()
service_proto_file_prefix = service_proto_file_path.removesuffix(".proto")
service_header_file_path = service_proto_file_prefix + ".pb.h"
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#ifndef GENERATED_API_${service.get_identify_upper_rule(output_render_path)}
#define GENERATED_API_${service.get_identify_upper_rule(output_render_path)}

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <${service_header_file_path}>
% if include_headers:
%   for include_header in include_headers:
%     if service_header_file_path != include_header:
#include <${include_header}>
%     endif
%   endfor
% endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

#include "rpc/rpc_common_types.h"
#include "dispatcher/dispatcher_type_defines.h"

#ifndef ${rpc_dllexport_decl}
#  define ${rpc_dllexport_decl} ATFW_UTIL_SYMBOL_VISIBLE
#endif

struct ss_msg_logic_index;

namespace atapp {
class etcd_discovery_node;
namespace protocol {
class atapp_metadata;
}  // namespace protocol
}  // namespace atapp

namespace rpc {
class context;
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    rpc_is_router_api = rpc.get_extension_field('atframework.rpc_options', lambda x: x.router_rpc, False)
    rpc_is_user_rpc = rpc.get_extension_field('atframework.rpc_options', lambda x: x.user_rpc, False)
    rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
    rpc_allow_ignore_discovery = rpc.get_extension_field('atframework.rpc_options', lambda x: x.allow_ignore_discovery, False)
    rpc_unicast_params_decl = ['context& __ctx']
    rpc_unicast_params_docs = ['__ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack']
    rpc_broadcast_params_decl = ['context& __ctx']
    rpc_broadcast_params_docs = ['__ctx               RPC context, you can get it from get_shared_context() of task_action or just create one on stack']
    if rpc_is_router_api:
        rpc_unicast_params_decl.extend(['uint32_t type_id', 'uint32_t zone_id', 'uint64_t object_id'])
        rpc_unicast_params_docs.extend([
            'type_id             router object type id',
            'zone_id             router object zone id, pass 0 if it has no zone id',
            'object_id           router object instance id'
        ])
    else:
        rpc_unicast_params_decl.append('const atapp::etcd_discovery_node& destination_server')
        rpc_unicast_params_docs.append('destination_server  target server')
        if rpc_is_user_rpc:
            rpc_unicast_params_decl.extend(['uint32_t zone_id', 'uint64_t user_id', "const std::string& open_id"])
            rpc_unicast_params_docs.extend([
                'zone_id             zone id that will be passsed into header',
                'user_id             user id that will be passsed into header',
                'open_id             open id that will be passsed into header'
            ])
    if rpc_is_user_rpc:
        rpc_broadcast_params_decl.extend(['uint32_t zone_id', 'uint64_t user_id', "const std::string& open_id"])
        rpc_broadcast_params_docs.extend([
            'zone_id             zone id that will be passsed into header',
            'user_id             user id that will be passsed into header',
            'open_id             open id that will be passsed into header'
        ])

    rpc_unicast_params_decl.append('{0} &request_body'.format(rpc.get_request().get_cpp_class_name()))
    rpc_unicast_params_docs.append('request_body       request body')
    rpc_broadcast_params_decl.append('{0} &request_body'.format(rpc.get_request().get_cpp_class_name()))
    rpc_broadcast_params_docs.append('request_body       request body')
    if not rpc_is_stream_mode:
        rpc_unicast_params_decl.append('{0} &response_body'.format(rpc.get_response().get_cpp_class_name()))
        rpc_unicast_params_docs.append('response_body       response body')
        if rpc.get_extension_field('atframework.rpc_options', lambda x: x.allow_no_wait, False):
            rpc_unicast_params_decl.append('bool __no_wait = false')
            rpc_unicast_params_decl.append('dispatcher_await_options* __wait_later = nullptr')
            rpc_unicast_params_docs.append('__no_wait           set true if not need to wait response')
            rpc_unicast_params_docs.append('__wait_later        set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here')
    if rpc_allow_ignore_discovery:
        rpc_unicast_params_decl.append('bool __ignore_discovery = false')
        rpc_unicast_params_docs.append('__ignore_discovery  set true if not need to wait response')
    if not rpc_is_router_api and rpc_is_stream_mode:
        rpc_return_type = 'rpc::always_ready_code_type'
    else:
        rpc_return_type = 'rpc::result_code_type'
    rpc_unicast_params_decl_legacy = []
    for param in rpc_unicast_params_decl:
        if 'const atapp::etcd_discovery_node& destination_server' == param:
            rpc_unicast_params_decl_legacy.append('uint64_t destination_server_id')
        else:
            rpc_unicast_params_decl_legacy.append(param)
%>
// ============ ${rpc.get_full_name()} ============
namespace packer {
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string& output, const ${rpc.get_request().get_cpp_class_name()}& input);
${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string& input, ${rpc.get_request().get_cpp_class_name()}& output);
% if not rpc_is_stream_mode:
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string& output, const ${rpc.get_response().get_cpp_class_name()}& input);
${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string& input, ${rpc.get_response().get_cpp_class_name()}& output);
% endif
}  // namespace packer
% if rpc.get_extension_field('atframework.rpc_options', lambda x: x.enable_broadcast, False):
namespace broadcast {
/**
 * @brief ${rpc.get_extension_field('atframework.rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_broadcast_params_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
 * @param index logic index
 * @param metadata metadata selector
%   for desc in rpc.get_extension_field('atframework.rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR ${rpc_dllexport_decl} ${rpc_return_type}
  ${rpc.get_name()}(
    ${', '.join(rpc_broadcast_params_decl)},
    const ss_msg_logic_index& index, ::atapp::protocol::atapp_metadata *metadata = nullptr);
}  // namespace broadcast
% endif

namespace unicast {
/**
 * @brief ${rpc.get_extension_field('atframework.rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_unicast_params_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
%   for desc in rpc.get_extension_field('atframework.rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR ${rpc_dllexport_decl} ${rpc_return_type}
  ${rpc.get_name()}(
    ${', '.join(rpc_unicast_params_decl)});
}  // namespace unicast

/**
 * @brief ${rpc.get_extension_field('atframework.rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_unicast_params_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
%   for desc in rpc.get_extension_field('atframework.rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
EXPLICIT_NODISCARD_ATTR ${rpc_dllexport_decl} ${rpc_return_type}
  ${rpc.get_name()}(
    ${', '.join(rpc_unicast_params_decl_legacy)});
% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}

#endif
