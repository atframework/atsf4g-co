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

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
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

#include <data/session.h>
#include <dispatcher/cs_msg_dispatcher.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
<%
rpc_common_codes_enable_stream_header = False
rpc_common_codes_enable_common = len(rpcs.values()) > 0

for rpc in rpcs.values():
    if not rpc.is_response_stream():
      continue

    if rpc.is_response_stream():
      rpc_common_codes_enable_stream_header = True

%>namespace {
% if rpc_common_codes_enable_common or rpc_common_codes_enable_stream_header:
template<class StringViewLikeT>
inline static atfw::util::nostd::string_view __to_string_view(const StringViewLikeT &input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}
% endif
% if rpc_common_codes_enable_common:
template<class TBodyType>
inline static int __pack_body(TBodyType &body, std::string *output, atfw::util::nostd::string_view rpc_full_name,
                                  atfw::util::nostd::string_view type_full_name) {
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

% if rpc_common_codes_enable_stream_header:
inline static int __setup_rpc_stream_header(atframework::CSMsgHead &head, atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name) {
  head.set_op_type(${project_namespace}::EN_MSG_OP_TYPE_STREAM);
  atframework::RpcStreamMeta* stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }
  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee("${service.get_full_name()}");
  stream_meta->set_rpc_name(rpc_full_name.data(), rpc_full_name.size());
  stream_meta->set_type_url(type_full_name.data(), type_full_name.size());

  return ${project_namespace}::err::EN_SUCCESS;

}
% endif
}

% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    if not rpc.is_response_stream():
        continue
    rpc_allow_no_wait = False
    rpc_params = ['context& __ctx', '{0} &__body'.format(rpc.get_response().get_cpp_class_name())]
%>
// ============ ${rpc.get_full_name()} ============
${service_dllexport_decl} rpc::always_ready_code_type send_${rpc.get_name()}(
  ${', '.join(rpc_params)}, session& __session) {
  atframework::CSMsg* msg_ptr = __ctx.create<atframework::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message for session [{:#x}, {}] failed",
               "${rpc.get_full_name()}",
               __session.get_key().node_id, __session.get_key().session_id);
    return {static_cast<rpc::always_ready_code_type::value_type>(${project_namespace}::err::EN_SYS_MALLOC)};
  }

  int res = __setup_rpc_stream_header(
    *msg_ptr->mutable_head(), "${rpc.get_full_name()}",
    __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));

  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_body(
    __body, msg_ptr->mutable_body_bin(), "${rpc.get_full_name()}",
    __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  if (!msg_ptr->has_head() || msg_ptr->head().timestamp() == 0) {
    msg_ptr->mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  __session.write_actor_log_body(__ctx, __body, *msg_ptr->mutable_head(), false);
  res = __session.send_msg_to_client(__ctx, *msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "${rpc.get_full_name()}",
               __session.get_key().node_id, __session.get_key().session_id,
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return {static_cast<rpc::always_ready_code_type::value_type>(res)};
}

${service_dllexport_decl} rpc::always_ready_code_type send_${rpc.get_name()}(
  ${', '.join(rpc_params)}, session& __session, uint64_t server_sequence) {
  atframework::CSMsg* msg_ptr = __ctx.create<atframework::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message for session [{:#x}, {}] failed",
               "${rpc.get_full_name()}",
               __session.get_key().node_id, __session.get_key().session_id);
    return {static_cast<rpc::always_ready_code_type::value_type>(${project_namespace}::err::EN_SYS_MALLOC)};
  }

  int res = __setup_rpc_stream_header(
    *msg_ptr->mutable_head(), "${rpc.get_full_name()}",
    __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));

  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_body(
    __body, msg_ptr->mutable_body_bin(), "${rpc.get_full_name()}",
    __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  if (!msg_ptr->has_head() || msg_ptr->head().timestamp() == 0) {
    msg_ptr->mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  __session.write_actor_log_body(__ctx, __body, *msg_ptr->mutable_head(), false);
  res = __session.send_msg_to_client(__ctx, *msg_ptr, server_sequence);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "${rpc.get_full_name()}",
               __session.get_key().node_id, __session.get_key().session_id,
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return {static_cast<rpc::always_ready_code_type::value_type>(res)};
}


${service_dllexport_decl} rpc::always_ready_code_type broadcast_${rpc.get_name()}(
  ${', '.join(rpc_params)}, uint64_t service_id) {
  atframework::CSMsg* msg_ptr = __ctx.create<atframework::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message to broadcast failed",
               "${rpc.get_full_name()}");
    return {static_cast<rpc::always_ready_code_type::value_type>(${project_namespace}::err::EN_SYS_MALLOC)};
  }

  int res = __setup_rpc_stream_header(
    *msg_ptr->mutable_head(), "${rpc.get_full_name()}",
    __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));

  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  res = __pack_body(
    __body, msg_ptr->mutable_body_bin(), "${rpc.get_full_name()}",
    __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }

  if (!msg_ptr->has_head() || msg_ptr->head().timestamp() == 0) {
    msg_ptr->mutable_head()->set_timestamp(::util::time::time_utility::get_now());
  }
  res = session::broadcast_msg_to_client(service_id, *msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} broadcast message  failed, result: {}({})",
               "${rpc.get_full_name()}",
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return {static_cast<rpc::always_ready_code_type::value_type>(res)};
}

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}
