## -*- coding: utf-8 -*-
<%!
import time
import os
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
#include <protocol/pbdesc/svr.local.table.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <router/router_manager_set.h>
#include <router/router_manager_base.h>
#include <router/router_player_manager.h>
#include <router/router_object_base.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
<%
rpc_common_codes_enable_redirect_info_log = False
rpc_common_codes_enable_redirect_warning_log = False
rpc_common_codes_enable_wait_response = False
rpc_common_codes_enable_stream_header = False
rpc_common_codes_enable_request_header = False
rpc_common_codes_enable_common = len(rpcs.values()) > 0

for rpc in rpcs.values():
    if not rpc_common_codes_enable_wait_response and not rpc.is_request_stream() and not rpc.is_response_stream():
        rpc_common_codes_enable_wait_response = True
    
    if not rpc_common_codes_enable_redirect_warning_log and rpc.get_extension_field('rpc_options', lambda x: x.warning_log_response_code, []):
        rpc_common_codes_enable_redirect_warning_log = True

    if not rpc_common_codes_enable_redirect_info_log and rpc.get_extension_field('rpc_options', lambda x: x.info_log_response_code, []):
        rpc_common_codes_enable_redirect_info_log = True

    if not rpc_common_codes_enable_stream_header or not rpc_common_codes_enable_request_header:
        rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
        rpc_allow_no_wait = False
        if not rpc_is_stream_mode:
            rpc_allow_no_wait = rpc.get_extension_field('rpc_options', lambda x: x.allow_no_wait, False)
        if rpc_is_stream_mode or rpc_allow_no_wait:
            rpc_common_codes_enable_stream_header = True
        if not rpc_is_stream_mode:
            rpc_common_codes_enable_request_header = True
%>namespace details {
% if rpc_common_codes_enable_common:
template<class TBodyType>
static inline int __pack_rpc_body(TBodyType &req_body, std::string *output, const char *rpc_full_name,
                                  const std::string &type_full_name) {
  if (false == req_body.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               req_body.InitializationErrorString());
    return ${project_namespace}::err::EN_SYS_PACK;
  } else {
    FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
               protobuf_mini_dumper_get_readable(req_body));
    return ${project_namespace}::err::EN_SUCCESS;
  }
}

static inline void __setup_tracer(rpc::context &__child_ctx, rpc::context::tracer &__tracer, ${project_namespace}::SSMsgHead &head,
                                  const char *rpc_full_name) {
  ::rpc::context::trace_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(ss_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT;

  __child_ctx.setup_tracer(__tracer, rpc_full_name, std::move(__trace_option));
  ::rpc::context::tracer::span_ptr_type __child_trace_span = __child_ctx.get_trace_span();
  if (__child_trace_span) {
    auto trace_span_head = head.mutable_rpc_trace();
    if (trace_span_head) {
      auto trace_context = __child_trace_span->GetContext();
      ::rpc::context::tracer::trace_id_span trace_id = trace_context.trace_id().Id();
      ::rpc::context::tracer::span_id_span span_id = trace_context.span_id().Id();

      trace_span_head->mutable_trace_id()->assign(reinterpret_cast<const char *>(trace_id.data()), trace_id.size());
      trace_span_head->mutable_span_id()->assign(reinterpret_cast<const char *>(span_id.data()), span_id.size());
      trace_span_head->set_kind(__trace_option.kind);
      trace_span_head->set_name(rpc_full_name);

      // trace_context.IsSampled();
    }
  }
}

% endif
% if rpc_common_codes_enable_stream_header:
static inline int __setup_rpc_stream_header(${project_namespace}::SSMsgHead &head, const char *rpc_full_name,
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
% endif
% if rpc_common_codes_enable_request_header:
static inline int __setup_rpc_request_header(${project_namespace}::SSMsgHead &head, task_manager::task_t &task,
                                             const char *rpc_full_name, const std::string &type_full_name) {
  head.set_src_task_id(task.get_id());
  head.set_op_type(${project_namespace}::EN_MSG_OP_TYPE_UNARY_REQUEST);
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }
  request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  request_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  request_meta->set_callee("${service.get_full_name()}");
  request_meta->set_rpc_name(rpc_full_name);
  request_meta->set_type_url(type_full_name);

  return ${project_namespace}::err::EN_SUCCESS;
}
% endif
% if rpc_common_codes_enable_redirect_info_log:
template<class TCode, class TConvertList>
static inline bool __redirect_rpc_result_to_info_log(TCode &origin_result, TConvertList&& convert_list, 
                                        const char *rpc_full_name, const std::string &type_full_name) {
  for (auto& check: convert_list) {
    if (origin_result == check) {
      FWLOGINFO("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name,
                origin_result, protobuf_mini_dumper_get_error_msg(origin_result)
      );

      return true;
    }
  }

  return false;
}
% endif
% if rpc_common_codes_enable_redirect_warning_log:
template<class TCode, class TConvertList>
static inline bool __redirect_rpc_result_to_warning_log(TCode &origin_result, TConvertList&& convert_list, 
                                        const char *rpc_full_name, const std::string &type_full_name) {
  for (auto& check: convert_list) {
    if (origin_result == check) {
      FWLOGWARNING("rpc {} wait for {} failed, res: {}({})", rpc_full_name, type_full_name,
                   origin_result, protobuf_mini_dumper_get_error_msg(origin_result)
      );

      return true;
    }
  }

  return false;
}
% endif
% if rpc_common_codes_enable_wait_response:
template<class TResponseBody>
static inline int __rpc_wait_and_unpack_response(rpc::context &__ctx, uint64_t rpc_sequence, TResponseBody &rsp_body, const char *rpc_full_name, 
                                            const std::string &type_full_name) {
  ${project_namespace}::SSMsg* rsp_msg_ptr = __ctx.create<${project_namespace}::SSMsg>();
  if (nullptr == rsp_msg_ptr) {
    FWLOGERROR("rpc {} create response message failed", rpc_full_name);
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }

  ${project_namespace}::SSMsg& rsp_msg = *rsp_msg_ptr;
  int res = rpc::wait(rsp_msg, rpc_sequence);
  if (res < 0) {
    return res;
  }

  if (rsp_msg.head().rpc_response().type_url() != type_full_name) {
    FWLOGERROR("rpc {} expect response message {}, but got {}", rpc_full_name, type_full_name,
               rsp_msg.head().rpc_response().type_url());
  }

  if (!rsp_msg.body_bin().empty()) {
    if (false == rsp_body.ParseFromString(rsp_msg.body_bin())) {
      FWLOGERROR("rpc {} parse message {} for failed, msg: {}", rpc_full_name, type_full_name,
                 rsp_body.InitializationErrorString());

      return ${project_namespace}::err::EN_SYS_UNPACK;
    } else {
      FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name,
                 protobuf_mini_dumper_get_readable(rsp_body));
    }
  }

  return rsp_msg.head().error_code();
}
% endif
}

% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
${result_clazz_name}::${result_clazz_name}() {}
${result_clazz_name}::${result_clazz_name}(int code): result(code) {}
${result_clazz_name}::operator int() const noexcept {
  if (!result.is_ready()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY;
  }

  const int* ret = result.data();
  if (nullptr == ret) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL;
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
    rpc_is_router_api = rpc.get_extension_field('rpc_options', lambda x: x.router_rpc, False)
    rpc_is_user_rpc = rpc.get_extension_field('rpc_options', lambda x: x.user_rpc, False)
    rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
    rpc_allow_no_wait = False
    if not rpc_is_stream_mode:
        rpc_allow_no_wait = rpc.get_extension_field('rpc_options', lambda x: x.allow_no_wait, False)
    if rpc_allow_no_wait:
        rpc_request_meta_pretty_prefix = '  '
    else:
        rpc_request_meta_pretty_prefix = ''
    rpc_params = ['context& __ctx']
    if rpc_is_router_api:
        rpc_params.extend(['uint32_t type_id', 'uint32_t zone_id', 'uint64_t object_id'])
    else:
        rpc_params.append('uint64_t dst_bus_id')
        if rpc_is_user_rpc:
            rpc_params.extend(['uint32_t zone_id', 'uint64_t user_id', "const std::string& open_id"])
    rpc_params.append('{0} &req_body'.format(rpc.get_request().get_cpp_class_name()))
    if not rpc_is_stream_mode:
        rpc_params.append('{0} &rsp_body'.format(rpc.get_response().get_cpp_class_name()))
        if rpc_allow_no_wait:
            rpc_params.append('bool __no_wait')
            rpc_params.append('uint64_t* __wait_later')
%>
// ============ ${rpc.get_full_name()} ============
${result_clazz_name} ${rpc.get_name()}(${', '.join(rpc_params)}) {
%   if rpc_is_router_api:
  if (object_id == 0 || type_id == 0) {
    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_PARAM);
  }
%   else:
  if (dst_bus_id == 0) {
    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_PARAM);
  }
%   endif

%   if rpc_is_router_api or not rpc_is_stream_mode:
  task_manager::task_t *task = task_manager::task_t::this_task();
  if (!task) {
    FWLOGERROR("rpc {} must be called in a task",
               "${rpc.get_full_name()}");
    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_RPC_NO_TASK);
  }
%   endif

  ${project_namespace}::SSMsg* req_msg_ptr = __ctx.create<${project_namespace}::SSMsg>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("rpc {} create request message failed",
               "${rpc.get_full_name()}");
    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_MALLOC);
  }

  int res;
  ${project_namespace}::SSMsg& req_msg = *req_msg_ptr;
  task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_local_server_id());
%   if rpc_allow_no_wait:
  if (__no_wait) {
%   endif
%   if rpc_is_stream_mode or rpc_allow_no_wait:
  ${rpc_request_meta_pretty_prefix}res = details::__setup_rpc_stream_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head(), "${rpc.get_full_name()}", 
    ${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()
  ${rpc_request_meta_pretty_prefix});
%   endif
%   if rpc_allow_no_wait:
  } else {
%   endif
%   if not rpc_is_stream_mode:
  ${rpc_request_meta_pretty_prefix}res = details::__setup_rpc_request_header(
    ${rpc_request_meta_pretty_prefix}*req_msg.mutable_head(), *task, "${rpc.get_full_name()}",
    ${rpc_request_meta_pretty_prefix}${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()
  ${rpc_request_meta_pretty_prefix});
%   endif
%   if rpc_allow_no_wait:
  }
%   endif
  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  res = details::__pack_rpc_body(req_body, req_msg.mutable_body_bin(),
                                 "${rpc.get_full_name()}", 
                                 ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
  if (res < 0) {
    return ${result_clazz_name}(res);
  }

  rpc::context __child_ctx(__ctx);
  rpc::context::tracer __tracer;
  details::__setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(),
                          "${rpc.get_full_name()}");

%   if rpc_is_user_rpc:
%     if rpc_is_router_api:
            req_msg.mutable_head()->set_player_user_id(object_id);
            req_msg.mutable_head()->set_player_zone_id(zone_id);
%     else:
            req_msg.mutable_head()->set_player_user_id(user_id);
            req_msg.mutable_head()->set_player_zone_id(zone_id);
            req_msg.mutable_head()->set_player_open_id(open_id);
%     endif
%   endif
%   if rpc_is_router_api:
%     if not rpc_is_user_rpc:
  if (type_id == router_player_manager::me()->get_type_id()) {
    req_msg.mutable_head()->set_player_user_id(object_id);
    req_msg.mutable_head()->set_player_zone_id(zone_id);
  }
%     endif

  router_object_base::key_t router_key(type_id, zone_id, object_id);
  router_object_base::trace_router(__child_ctx, router_key);

  router_manager_base* router_manager = router_manager_set::me()->get_manager(type_id);
  if (nullptr == router_manager) {
    FWLOGERROR("rpc {} can not get router manager of type {}",
               "${rpc.get_full_name()}", type_id);
    return ${result_clazz_name}(__tracer.return_code(${project_namespace}::err::EN_SYS_NOT_SUPPORT));
  }

  uint64_t rpc_sequence = 0;
  res = router_manager->send_msg(router_key, std::move(req_msg), rpc_sequence);
%   else:
  res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
%   endif
%   if rpc_is_stream_mode:
  if (res < 0) {
%     if rpc.get_extension_field('rpc_options', lambda x: x.warning_log_response_code, []):
    const int warning_codes[] = {${', '.join(rpc.get_extension_field('rpc_options', lambda x: x.warning_log_response_code, []))}};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
        return ${result_clazz_name}(__tracer.return_code(res));
  }
%     endif
%     if rpc.get_extension_field('rpc_options', lambda x: x.info_log_response_code, []):
  const int info_codes[] = {${', '.join(rpc.get_extension_field('rpc_options', lambda x: x.info_log_response_code, []))}};
  if (details::__redirect_rpc_result_to_info_log(res, info_codes,
      "${rpc.get_full_name()}",
      ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
    return ${result_clazz_name}(__tracer.return_code(res));
  }
%     endif
    FWLOGERROR("rpc {} call failed, res: {}({})",
               "${rpc.get_full_name()}",
               res, protobuf_mini_dumper_get_error_msg(res)
    );
  }
  return ${result_clazz_name}(__tracer.return_code(res));
%   else:
  do {
    uint64_t rpc_sequence = req_msg.head().sequence();
%     if rpc_allow_no_wait:
    if (__no_wait) {
      break;
    } else if (nullptr != __wait_later) {
      *__wait_later = rpc_sequence;
      // need to call rpc::wait(...) to wait this rpc sequence later
      break;
    }
%     endif
    if (res < 0) {
      break;
    }
    res = details::__rpc_wait_and_unpack_response(__ctx, rpc_sequence, rsp_body,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name());
  } while (false);

  if (res < 0) {
%     if rpc.get_extension_field('rpc_options', lambda x: x.warning_log_response_code, []):
    const int warning_codes[] = {${', '.join(rpc.get_extension_field('rpc_options', lambda x: x.warning_log_response_code, []))}};
    if (details::__redirect_rpc_result_to_warning_log(res, warning_codes,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
      return ${result_clazz_name}(__tracer.return_code(res));
    }
%     endif
%     if warning_log_codes in rpc.get_extension_field('rpc_options', lambda x: x.info_log_response_code, []):
    const int info_codes[] = {${', '.join(rpc.get_extension_field('rpc_options', lambda x: x.info_log_response_code, []))}};
    if (details::__redirect_rpc_result_to_info_log(res, info_codes,
        "${rpc.get_full_name()}",
        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name())) {
      return ${result_clazz_name}(__tracer.return_code(res));
    }
%     endif
      FWLOGERROR("rpc {} call failed, res: {}({})",
                 "${rpc.get_full_name()}",
                 res, protobuf_mini_dumper_get_error_msg(res)
      );
  }

  return ${result_clazz_name}(__tracer.return_code(res));
%   endif
}
% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}