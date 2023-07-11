## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${service.get_name_lower_rule()}.h"

#include <log/log_wrapper.h>

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

namespace rpc {
<%
rpc_common_codes_enable_stream_header = False
rpc_common_codes_enable_request_header = False
rpc_common_codes_enable_common = len(rpcs.values()) > 0

for rpc in rpcs.values():
    if rpc.get_request_descriptor().full_name == "google.protobuf.Empty":
        continue

    if not rpc_common_codes_enable_stream_header or not rpc_common_codes_enable_request_header:
        if rpc.is_request_stream():
            rpc_common_codes_enable_stream_header = True
        else:
            rpc_common_codes_enable_request_header = True

%>namespace details {
% if rpc_common_codes_enable_common:
template<class TBodyType>
static inline int __pack_body(const TBodyType &body, std::string *output, const char *rpc_full_name,
                                  const std::string &type_full_name) {
  if (false == body.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               body.InitializationErrorString());
    return ${project_namespace}::EN_ERR_SYSTEM;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name, body.DebugString());
    return ${project_namespace}::EN_SUCCESS;
  }
}
% endif

% if rpc_common_codes_enable_stream_header:
static inline int __setup_rpc_stream_header(atframework::CSMsgHead &head, const char *rpc_full_name,
                                            const std::string &type_full_name) {
  head.set_op_type(${project_namespace}::EN_MSG_OP_TYPE_STREAM);
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return ${project_namespace}::EN_ERR_SYSTEM;
  }
  stream_meta->set_version(client_simulator::get_atframework_settings().rpc_version());
  stream_meta->set_caller("client_simulator");
  stream_meta->set_callee("${service.get_full_name()}");
  stream_meta->set_rpc_name(rpc_full_name);
  stream_meta->set_type_url(type_full_name);

  return ${project_namespace}::EN_SUCCESS;

}
% endif

% if rpc_common_codes_enable_request_header:
static inline int __setup_rpc_request_header(atframework::CSMsgHead &head, const char *rpc_full_name,
                                            const std::string &type_full_name) {
  head.set_op_type(${project_namespace}::EN_MSG_OP_TYPE_UNARY_REQUEST);
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return ${project_namespace}::EN_ERR_SYSTEM;
  }
  request_meta->set_version(client_simulator::get_atframework_settings().rpc_version());
  request_meta->set_caller("client_simulator");
  request_meta->set_callee("${service.get_full_name()}");
  request_meta->set_rpc_name(rpc_full_name);
  request_meta->set_type_url(type_full_name);

  return ${project_namespace}::EN_SUCCESS;
}
% endif
}  // namespace details

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
// ============ ${rpc.get_full_name()} ============
${rpc_dllexport_decl} int package_${rpc.get_name()}(${', '.join(rpc_params)}) {
% if rpc.is_request_stream():
  int res = details::__setup_rpc_stream_header(
    *__output.mutable_head(), "${rpc.get_full_name()}",
    ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
% else:
  int res = details::__setup_rpc_request_header(
    *__output.mutable_head(), "${rpc.get_full_name()}",
    ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
% endif

  if (res < 0) {
    return res;
  }

  res = details::__pack_body(__body, __output.mutable_body_bin(),
                            "${rpc.get_full_name()}",
                            ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());

  return res;
}

${rpc_dllexport_decl} gsl::string_view get_full_name_of_${rpc.get_name()}() {
  return "${rpc.get_full_name()}";
}

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}  // namespace rpc
