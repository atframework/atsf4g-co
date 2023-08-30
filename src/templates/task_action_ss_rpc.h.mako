## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
task_class_name = os.path.splitext(os.path.basename(output_render_path))[0]
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${local_vcs_user_name} with ${generator} at ${time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()) }

#pragma once

#include <config/compile_optimize.h>

% if include_headers:
// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

%   for include_header in include_headers:
#include <${include_header}>
%   endfor

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on
% endif

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ${service_dllexport_decl}
#  define ${service_dllexport_decl} UTIL_SYMBOL_VISIBLE
#endif

class ${task_class_name} : public task_action_ss_rpc_base<${rpc.get_request().get_cpp_class_name()}, ${rpc.get_response().get_cpp_class_name()}> {
 public:
  using base_type = task_action_ss_rpc_base<${rpc.get_request().get_cpp_class_name()}, ${rpc.get_response().get_cpp_class_name()}>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type  = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ${service_dllexport_decl} explicit ${task_class_name}(dispatcher_start_data_type&& param);
  ${service_dllexport_decl} ~${task_class_name}();

  ${service_dllexport_decl} const char *name() const override;

  ${service_dllexport_decl} result_type operator()() override;

  ${service_dllexport_decl} int on_success() override;
  ${service_dllexport_decl} int on_failed() override;

% if rpc.get_extension_field('rpc_options', lambda x: x.router_rpc, False) and rpc.get_extension_field('rpc_options', lambda x: x.router_ignore_offline, False):
  ${service_dllexport_decl} bool is_router_offline_ignored() const override;
% endif
};
