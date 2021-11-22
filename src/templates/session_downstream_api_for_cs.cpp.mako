## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
result_clazz_name = service.get_name_lower_rule() + '_result_t'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${service.get_name_lower_rule()}.h"

#include <log/log_wrapper.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <data/session.h>
#include <dispatcher/cs_msg_dispatcher.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
<%
rpc_common_codes_enable_common = len(rpcs.values()) > 0

for rpc in rpcs.values():
    if not rpc.is_response_stream():
        continue

%>namespace details {
% if rpc_common_codes_enable_common:
template<class TBodyType>
static inline int __pack_body(TBodyType &body, std::string *output, const char *rpc_full_name,
                                  const std::string &type_full_name) {
  if (false == body.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               body.InitializationErrorString());
    return ${project_namespace}::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
               protobuf_mini_dumper_get_readable(body));
    return ${project_namespace}::err::EN_SUCCESS;
  }
}
% endif

static inline int __setup_rpc_stream_header(${project_namespace}::CSMsgHead &head, const char *rpc_full_name,
                                            const std::string &type_full_name) {
  head.set_op_type(${project_namespace}::EN_MSG_OP_TYPE_STREAM);
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee("${service.get_full_name()}");
  stream_meta->set_rpc_name(rpc_full_name);
  stream_meta->set_type_url(type_full_name);

  return ${project_namespace}::err::EN_SUCCESS;

}
}

% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
${result_clazz_name}::${result_clazz_name}() {}
${result_clazz_name}::${result_clazz_name}(int code): result(code) {}
${result_clazz_name}::operator int() const noexcept {
  if (!result.is_ready()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
  }

  const int* ret = result.data();
  if (nullptr == ret) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
  }

  return *ret;
}

bool ${result_clazz_name}::is_success() const noexcept {
  if (!result.is_ready()) {
    return false;
  }

  const int* ret = result.data();
  if (nullptr == ret) {
    return false;
  }

  return *ret >= 0;
}

bool ${result_clazz_name}::is_error() const noexcept {
  if (!result.is_ready()) {
    return false;
  }

  const int* ret = result.data();
  if (nullptr == ret) {
    return false;
  }

  return *ret < 0;
}
% for rpc in rpcs.values():
<%
    if not rpc.is_response_stream():
        continue
    rpc_allow_no_wait = False
    rpc_params = ['context& __ctx', '{0} &__body'.format(rpc.get_response().get_cpp_class_name())]
%>
// ============ ${rpc.get_full_name()} ============
${result_clazz_name} send_${rpc.get_name()}(
  ${', '.join(rpc_params)}, session& __session) {
  ${project_namespace}::CSMsg* msg_ptr = __ctx.create<${project_namespace}::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message for session [{:#x}, {}] failed",
               "${rpc.get_full_name()}",
               __session.get_key().bus_id, __session.get_key().session_id);
    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_MALLOC);
  }

  int res = details::__setup_rpc_stream_header(
    *msg_ptr->mutable_head(), "${rpc.get_full_name()}",
    ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());

  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  res = details::__pack_body(__body, msg_ptr->mutable_body_bin(),
                            "${rpc.get_full_name()}",
                            ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());
  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  res = __session.send_msg_to_client(*msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "${rpc.get_full_name()}",
               __session.get_key().bus_id, __session.get_key().session_id,
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return ${result_clazz_name}(res);
}

${result_clazz_name} send_${rpc.get_name()}(
  ${', '.join(rpc_params)}, session& __session, uint64_t server_sequence) {
  ${project_namespace}::CSMsg* msg_ptr = __ctx.create<${project_namespace}::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message for session [{:#x}, {}] failed",
               "${rpc.get_full_name()}",
               __session.get_key().bus_id, __session.get_key().session_id);
    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_MALLOC);
  }

  int res = details::__setup_rpc_stream_header(
    *msg_ptr->mutable_head(), "${rpc.get_full_name()}",
    ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());

  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  res = details::__pack_body(__body, msg_ptr->mutable_body_bin(),
                            "${rpc.get_full_name()}",
                            ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());
  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  res = __session.send_msg_to_client(*msg_ptr, server_sequence);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "${rpc.get_full_name()}",
               __session.get_key().bus_id, __session.get_key().session_id,
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return ${result_clazz_name}(res);
}


${result_clazz_name} broadcast_${rpc.get_name()}(
  ${', '.join(rpc_params)}, uint64_t service_id) {
  ${project_namespace}::CSMsg* msg_ptr = __ctx.create<${project_namespace}::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message to broadcast failed",
               "${rpc.get_full_name()}");
    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_MALLOC);
  }

  int res = details::__setup_rpc_stream_header(
    *msg_ptr->mutable_head(), "${rpc.get_full_name()}",
    ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());

  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  res = details::__pack_body(__body, msg_ptr->mutable_body_bin(),
                            "${rpc.get_full_name()}",
                            ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());
  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  res = session::broadcast_msg_to_client(service_id, *msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} broadcast message  failed, result: {}({})",
               "${rpc.get_full_name()}",
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return ${result_clazz_name}(res);
}

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}