## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
task_class_name = os.path.splitext(os.path.basename(output_render_path))[0]
rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${local_vcs_user_name} with ${generator} at ${time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()) }

#include "${task_class_name}.h"

#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

${service_dllexport_decl} ${task_class_name}::${task_class_name}(dispatcher_start_data_type &&param)
    : base_type(std::move(param)) {}

${service_dllexport_decl} ${task_class_name}::~${task_class_name}() {}

${service_dllexport_decl} const char *${task_class_name}::name() const { return "${task_class_name}"; }

${service_dllexport_decl} ${task_class_name}::result_type ${task_class_name}::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type &req_body = get_request_body();
% if rpc_is_stream_mode:
  disable_response_message();
% else:
  EXPLICIT_UNUSED_ATTR rpc_response_type &rsp_body = get_response_body();
% endif

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

${service_dllexport_decl} int ${task_class_name}::on_success() { return get_result(); }

${service_dllexport_decl} int ${task_class_name}::on_failed() { return get_result(); }