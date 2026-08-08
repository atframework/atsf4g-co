## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
result_clazz_name = service.get_name_lower_rule() + '_result_t'

output_render_dir = os.path.dirname(output_render_path)
cpp_include_base_dir = ''
if output_render_dir and not os.path.isabs(output_render_dir):
    cpp_include_base_dir = output_render_dir + '/'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${cpp_include_base_dir}${service.get_name_lower_rule()}.atfw.gen.h"

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
#include <rpc/internal/rpc_template_cs_message.h>

#include <gsl/select-gsl.h>

namespace rpc {
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
// ============ ${rpc.get_service().get_full_name()}/${rpc.get_name()} ============
${service_dllexport_decl} gsl::string_view get_full_name_of_${rpc.get_name()}() {
  return "${rpc.get_service().get_full_name()}/${rpc.get_name()}";
}

<%
    if not rpc.is_response_stream():
        continue
    rpc_allow_no_wait = False
    rpc_params = ['context& __ctx', '{0} &__body'.format(rpc.get_response().get_cpp_class_name())]
%>
${service_dllexport_decl} rpc::always_ready_code_type send_${rpc.get_name()}(
  ${', '.join(rpc_params)}, session& __session) {
  atframework::CSMsg* msg_ptr = __ctx.create<atframework::CSMsg>();
  if (nullptr == msg_ptr) {
    FWLOGERROR("rpc {} create request message for session [{:#x}, {}] failed",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
               __session.get_key().node_id, __session.get_key().session_id);
    return {static_cast<rpc::always_ready_code_type::value_type>(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC)};
  }

  int res = internal::pack_cs_stream_message(
    *msg_ptr, __body, "${service.get_full_name()}", "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }
  __session.write_actor_log_body(__ctx, __body, *msg_ptr->mutable_head(), false);
  res = __session.send_msg_to_client(__ctx, *msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
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
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
               __session.get_key().node_id, __session.get_key().session_id);
    return {static_cast<rpc::always_ready_code_type::value_type>(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC)};
  }

  int res = internal::pack_cs_stream_message(
    *msg_ptr, __body, "${service.get_full_name()}", "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }
  __session.write_actor_log_body(__ctx, __body, *msg_ptr->mutable_head(), false);
  res = __session.send_msg_to_client(__ctx, *msg_ptr, server_sequence);
  if (res < 0) {
    FWLOGERROR("rpc {} send message to session [{:#x}, {}] failed, result: {}({})",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
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
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}");
    return {static_cast<rpc::always_ready_code_type::value_type>(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC)};
  }

  int res = internal::pack_cs_stream_message(
    *msg_ptr, __body, "${service.get_full_name()}", "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
    internal::to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    return {static_cast<rpc::always_ready_code_type::value_type>(res)};
  }
  res = session::broadcast_msg_to_client(service_id, *msg_ptr);
  if (res < 0) {
    FWLOGERROR("rpc {} broadcast message  failed, result: {}({})",
               "${rpc.get_service().get_full_name()}/${rpc.get_name()}",
               res, protobuf_mini_dumper_get_error_msg(res));
  }

  return {static_cast<rpc::always_ready_code_type::value_type>(res)};
}

% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}
