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

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/player.h>

${task_class_name}::${task_class_name}(dispatcher_start_data_t&& param) : base_type(COPP_MACRO_STD_MOVE(param)) {}
${task_class_name}::~${task_class_name}() {}

const char *${task_class_name}::name() const {
  return "${task_class_name}";
}

int ${task_class_name}::operator()() {
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
    set_response_code(PROJECT_SERVER_FRAME_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    return ${project_namespace}::err::EN_SUCCESS;
  }

  // TODO ...

  return ${project_namespace}::err::EN_SUCCESS;
}

int ${task_class_name}::on_success() { return get_result(); }

int ${task_class_name}::on_failed() { return get_result(); }

% if rpc.get_extension_field('rpc_options', lambda x: x.router_rpc, False) and rpc.get_extension_field('rpc_options', lambda x: x.router_ignore_offline, False):
bool ${task_class_name}::is_router_offline_ignored() const { return true; }
% endif