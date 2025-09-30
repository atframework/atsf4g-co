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

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

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

%>namespace {
% if rpc_common_codes_enable_common:
template<class StringViewLikeT>
inline static atfw::util::nostd::string_view __to_string_view(const StringViewLikeT &input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}

template<class TBodyType>
inline static int __pack_body(const TBodyType &body, std::string *output, atfw::util::nostd::string_view rpc_full_name,
                                  atfw::util::nostd::string_view type_full_name) {
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
inline static int __setup_rpc_stream_header(atframework::CSMsgHead &head, atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name) {
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return ${project_namespace}::EN_ERR_SYSTEM;
  }
  stream_meta->set_version(client_simulator::get_atframework_settings().rpc_version());
  stream_meta->set_caller("client_simulator");
  stream_meta->set_callee("${service.get_full_name()}");
  stream_meta->set_rpc_name(rpc_full_name.data(), rpc_full_name.size());
  stream_meta->set_type_url(type_full_name.data(), type_full_name.size());
  stream_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  stream_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return ${project_namespace}::EN_SUCCESS;

}
% endif

% if rpc_common_codes_enable_request_header:
inline static int __setup_rpc_request_header(atframework::CSMsgHead &head, atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name) {
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return ${project_namespace}::EN_ERR_SYSTEM;
  }
  request_meta->set_version(client_simulator::get_atframework_settings().rpc_version());
  request_meta->set_caller("client_simulator");
  request_meta->set_callee("${service.get_full_name()}");
  request_meta->set_rpc_name(rpc_full_name.data(), rpc_full_name.size());
  request_meta->set_type_url(type_full_name.data(), type_full_name.size());

  return ${project_namespace}::EN_SUCCESS;
}
% endif
}  // namespace

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
  auto request_full_name = __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
% if rpc.is_request_stream():
  int res = __setup_rpc_stream_header(
    *__output.mutable_head(), "${rpc.get_full_name()}", request_full_name);
% else:
  int res = __setup_rpc_request_header(
    *__output.mutable_head(), "${rpc.get_full_name()}", request_full_name);
% endif

  if (res < 0) {
    return res;
  }

  res = __pack_body(
    __body, __output.mutable_body_bin(), "${rpc.get_full_name()}", request_full_name);

  return res;
}

${rpc_dllexport_decl} atfw::util::nostd::string_view get_full_name_of_${rpc.get_name()}() {
  return "${rpc.get_full_name()}";
}

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}  // namespace rpc
