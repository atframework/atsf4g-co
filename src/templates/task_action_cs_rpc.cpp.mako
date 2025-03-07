## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%><%
task_class_name = os.path.splitext(os.path.basename(output_render_path))[0]
rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${local_vcs_user_name} with ${generator} at ${time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()) }

#include "${task_class_name}.h"

#include <std/explicit_declare.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/player.h>

${service_dllexport_decl} ${task_class_name}::${task_class_name}(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}

${service_dllexport_decl} ${task_class_name}::~${task_class_name}() {}

${service_dllexport_decl} const char *${task_class_name}::name() const {
  return "${task_class_name}";
}

${service_dllexport_decl} ${task_class_name}::result_type ${task_class_name}::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
% if rpc.is_request_stream() or rpc.is_response_stream():
  // Stream request or stream response, just ignore auto response
  disable_response_message();
% else:
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();
%   if rpc.get_extension_field('rpc_options', lambda x: x.allow_no_wait, False):
  if (is_stream_rpc()) {
    disable_response_message();
  }
%   endif
% endif

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(${project_namespace}::err::EN_SUCCESS);
  }

  // TODO ...

  TASK_ACTION_RETURN_CODE(${project_namespace}::err::EN_SUCCESS);
}

${service_dllexport_decl} int ${task_class_name}::on_success() { return get_result(); }

${service_dllexport_decl} int ${task_class_name}::on_failed() { return get_result(); }

% if rpc.get_extension_field('rpc_options', lambda x: x.router_rpc, False) and rpc.get_extension_field('rpc_options', lambda x: x.router_ignore_offline, False):
${service_dllexport_decl} bool ${task_class_name}::is_router_offline_ignored() const { return true; }
% endif
