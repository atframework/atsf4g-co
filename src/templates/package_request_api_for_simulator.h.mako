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

#include <protocol/pbdesc/com.protocol.pb.h>
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>

#include <config/server_frame_build_feature.h>

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <string>

#ifndef ${rpc_dllexport_decl}
#  define ${rpc_dllexport_decl} ATFW_UTIL_SYMBOL_VISIBLE
#endif

namespace rpc {
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    # Only generate not empty calls
    if rpc.get_request_descriptor().full_name == "google.protobuf.Empty":
        continue
    rpc_params = [
      'atframework::CSMsg& __output',
      'const {0} &__body'.format(rpc.get_request().get_cpp_class_name())
    ]
    rpc_param_docs = [
        '__output         Message container',
        '__body           Message body to package'
    ]
%>
// ============ ${rpc.get_full_name()} ============
/**
 * @brief package ${rpc.get_request().get_cpp_class_name()} for ${rpc.get_name()}
 * @brief ${rpc.get_extension_field('rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_param_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
%   for desc in rpc.get_extension_field('rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
${rpc_dllexport_decl} int package_${rpc.get_name()}(${', '.join(rpc_params)});

/**
 * @brief get full rpc name of ${rpc.get_name()}
 * @return full rpc name of ${rpc.get_name()}
 */
${rpc_dllexport_decl} gsl::string_view get_full_name_of_${rpc.get_name()}();

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}  // namespace rpc

#endif
