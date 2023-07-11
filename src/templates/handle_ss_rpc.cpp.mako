## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "handle_ss_rpc_${service.get_name_lower_rule()}.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

#include <dispatcher/ss_msg_dispatcher.h>

% for rpc in rpcs.values():
%   if rpc_include_prefix and os.path.dirname(rpc_include_prefix) != '.':
#include <${rpc_include_prefix}/${rpc.get_extension_field("rpc_options", lambda x: x.module_name, "action")}/task_action_${rpc.get_name()}.h>
%   else:
#include <${rpc.get_extension_field("rpc_options", lambda x: x.module_name, "action")}/task_action_${rpc.get_name()}.h>
%   endif
% endfor

namespace handle {
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
${service_dllexport_decl} int register_handles_for_${service.get_name_lower_rule()}() {
  int ret = 0;
% for rpc in rpcs.values():
  REG_TASK_RPC_HANDLE(ss_msg_dispatcher, ret, task_action_${rpc.get_name()}, ${service.get_cpp_class_name()}::descriptor(), "${rpc.get_full_name()}");
% endfor
  return ret;
}
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}

