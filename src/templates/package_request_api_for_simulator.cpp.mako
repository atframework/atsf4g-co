## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())

output_render_dir = os.path.dirname(output_render_path)
cpp_include_base_dir = ''
if output_render_dir and not os.path.isabs(output_render_dir):
    cpp_include_base_dir = output_render_dir + '/'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${cpp_include_base_dir}${service.get_name_lower_rule()}.atfw.gen.h"

#include <nostd/string_view.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.pb.h>
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <utility/client_simulator.h>

#include <rpc/internal/rpc_template_cs_message.h>

namespace rpc {
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    if rpc.get_request_descriptor().full_name == "google.protobuf.Empty":
        continue
    rpc_allow_no_wait = False
    rpc_params = [
      'atframework::CSMsg& __output',
      'const {0} &__body'.format(rpc.get_request().get_cpp_class_name())
    ]
%>
// ============ ${rpc.get_service().get_full_name()}/${rpc.get_name()} ============
${rpc_dllexport_decl} int package_${rpc.get_name()}(${', '.join(rpc_params)}) {
  auto request_full_name = internal::to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
% if rpc.is_request_stream():
  int res = internal::setup_cs_rpc_stream_header(
    *__output.mutable_head(), client_simulator::get_atframework_settings().rpc_version(), "client_simulator",
    "${service.get_full_name()}", "${rpc.get_service().get_full_name()}/${rpc.get_name()}", request_full_name);
% else:
  int res = internal::setup_cs_rpc_request_header(
    *__output.mutable_head(), client_simulator::get_atframework_settings().rpc_version(), "client_simulator",
    "${service.get_full_name()}", "${rpc.get_service().get_full_name()}/${rpc.get_name()}", request_full_name);
% endif

  if (res < 0) {
    return res;
  }

  res = internal::pack_rpc_body(
    __body, __output.mutable_body_bin(), "${rpc.get_service().get_full_name()}/${rpc.get_name()}", request_full_name);

  return res;
}

${rpc_dllexport_decl} atfw::util::nostd::string_view get_full_name_of_${rpc.get_name()}() {
  return "${rpc.get_service().get_full_name()}/${rpc.get_name()}";
}

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}  // namespace rpc
