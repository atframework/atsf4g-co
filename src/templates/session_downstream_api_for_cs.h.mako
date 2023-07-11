## -*- coding: utf-8 -*-
<%!
import time
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
result_clazz_name = service.get_name_lower_rule() + '_result_t'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#ifndef GENERATED_API_${service.get_identify_upper_rule(output_render_path)}
#define GENERATED_API_${service.get_identify_upper_rule(output_render_path)}

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.protocol.pb.h>
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "rpc/rpc_common_types.h"

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <string>

#ifndef ${service_dllexport_decl}
#  define ${service_dllexport_decl} UTIL_SYMBOL_VISIBLE
#endif

class session;
namespace rpc {
class context;
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    # Only generate downstream calls
    if not rpc.is_response_stream():
        continue
    rpc_params = ['context& __ctx', '{0} &__body'.format(rpc.get_response().get_cpp_class_name())]
    rpc_param_docs = [
        '__ctx            RPC context, you can get it from get_shared_context() of task_action or just create one on stack',
        '__body           Message body to send'
    ]
%>
// ============ ${rpc.get_full_name()} ============
/**
 * @brief send ${rpc.get_response().get_cpp_class_name()} for ${rpc.get_name()} to session
 * @brief ${rpc.get_extension_field('rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_param_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
 * @param __session        Client session
%   for desc in rpc.get_extension_field('rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
${service_dllexport_decl} rpc::always_ready_code_type send_${rpc.get_name()}(
  ${', '.join(rpc_params)}, session& __session);

/**
 * @brief send ${rpc.get_response().get_cpp_class_name()} for ${rpc.get_name()} to session
 * @brief ${rpc.get_extension_field('rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_param_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
 * @param __session        Client session
 * @param server_sequence  Use specify server sequence
%   for desc in rpc.get_extension_field('rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
${service_dllexport_decl} rpc::always_ready_code_type send_${rpc.get_name()}(
  ${', '.join(rpc_params)}, session& __session, uint64_t server_sequence);

/**
 * @brief broadcast ${rpc.get_response().get_cpp_class_name()} for ${rpc.get_name()} to gateway service
 * @brief ${rpc.get_extension_field('rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_param_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
 * @param service_id       Service ID
%   for desc in rpc.get_extension_field('rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
${service_dllexport_decl} rpc::always_ready_code_type broadcast_${rpc.get_name()}(
  ${', '.join(rpc_params)}, uint64_t service_id);
% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}

#endif
