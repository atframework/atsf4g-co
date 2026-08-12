## -*- coding: utf-8 -*-
<%!
import time
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#ifndef GENERATED_API_${service.get_identify_upper_rule(output_render_path)}
#define GENERATED_API_${service.get_identify_upper_rule(output_render_path)}

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/orbit.common.pb.h>
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <string>

#include <gsl/select-gsl.h>

#include <dispatcher/dispatcher_type_defines.h>
#include <rpc/rpc_common_types.h>

#ifndef ${rpc_dllexport_decl}
#  define ${rpc_dllexport_decl} ATFW_UTIL_SYMBOL_VISIBLE
#endif

namespace rpc {
class context;
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
    rpc_params = ['context& __ctx', 'const std::string &client_id', '{0} &req_body'.format(rpc.get_request().get_cpp_class_name())]
    rpc_param_docs = [
        '__ctx                 RPC context, you can get it from get_shared_context() of task_action or just create one on stack',
        'client_id             orbit client_id',
        'req_body              request body'
    ]
    if not rpc_is_stream_mode:
        rpc_params.append('{0} &rsp_body'.format(rpc.get_response().get_cpp_class_name()))
        rpc_param_docs.append('rsp_body              response body')
        if rpc.get_extension_field('rpc_options', lambda x: x.allow_no_wait, False):
            rpc_params.append('bool __no_wait = false')
            rpc_params.append('dispatcher_await_options* __wait_later = nullptr')
            rpc_param_docs.append('__no_wait             set true if no need to wait response')
            rpc_param_docs.append('__wait_later          set not nullptr if caller wants to wait this RPC later')
        rpc_return_type = 'rpc::result_code_type'
    else:
        rpc_return_type = 'rpc::always_ready_code_type'
%>
// ============ "${rpc.get_service().get_full_name()}/${rpc.get_name()}" ============
namespace packer {
/**
 * @brief get full rpc name of ${rpc.get_name()}
 * @return full rpc name of ${rpc.get_name()}
 * @note  Use this instead of a hardcoded name when registering mock rules or expectations.
 */
${rpc_dllexport_decl} gsl::string_view get_full_name_of_${rpc.get_name()}();
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string &output, const ${rpc.get_request().get_cpp_class_name()} &input);
${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string &input, ${rpc.get_request().get_cpp_class_name()} &output);
% if not rpc_is_stream_mode:
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string &output, const ${rpc.get_response().get_cpp_class_name()} &input);
${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string &input, ${rpc.get_response().get_cpp_class_name()} &output);
% endif
}  // namespace packer

/**
 * @brief ${rpc.get_extension_field('rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_param_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
ATFW_EXPLICIT_NODISCARD_ATTR ${rpc_dllexport_decl} ${rpc_return_type} ${rpc.get_name()}(${', '.join(rpc_params)});
% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}  // namespace rpc

#endif