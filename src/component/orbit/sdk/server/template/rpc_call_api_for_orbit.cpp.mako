## -*- coding: utf-8 -*-
<%!
import time
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
def rpc_return_result_code_sentense(input):
  return 'RPC_RETURN_CODE({});'.format(input)

def rpc_return_always_ready_code_sentense(input):
  return 'return {static_cast<rpc::always_ready_code_type::value_type>(' + input + ')};'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#include "${service.get_name_lower_rule()}.atfw.gen.h"

#include <nostd/string_view.h>
#include <nostd/utility_data_size.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
% if include_headers:
%   for include_header in include_headers:
#include <${include_header}>
%   endfor
% endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/semconv/incubating/rpc_attributes.h>

#include <config/logic_config.h>

#include <logic/orbit_msg_dispatcher.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

namespace rpc {
namespace {
template <class StringViewLikeT>
inline static atfw::util::nostd::string_view __to_string_view(const StringViewLikeT &input) {
  return {atfw::util::nostd::data(input), atfw::util::nostd::size(input)};
}

template <class TBodyType>
inline static int __pack_rpc_body(TBodyType &&input, std::string *output,
                                  atfw::util::nostd::string_view rpc_full_name,
                                  atfw::util::nostd::string_view type_full_name) {
  if (false == input.SerializeToString(output)) {
    FWLOGERROR("rpc {} serialize message {} failed, msg: {}", rpc_full_name, type_full_name,
               input.InitializationErrorString());
    return ${project_namespace}::err::EN_SYS_PACK;
  }

  FWLOGDEBUG("rpc {} serialize message {} success:\n{}", rpc_full_name, type_full_name,
             protobuf_mini_dumper_get_readable(input));
  return ${project_namespace}::err::EN_SUCCESS;
}

template <class TBodyType>
inline static int __unpack_rpc_body(TBodyType &&output, const std::string &input,
                                    atfw::util::nostd::string_view rpc_full_name,
                                    atfw::util::nostd::string_view type_full_name) {
  if (false == output.ParseFromString(input)) {
    FWLOGERROR("rpc {} parse message {} failed, msg: {}", rpc_full_name, type_full_name,
               output.InitializationErrorString());
    return ${project_namespace}::err::EN_SYS_PACK;
  }

  FWLOGDEBUG("rpc {} parse message {} success:\n{}", rpc_full_name, type_full_name,
             protobuf_mini_dumper_get_readable(output));
  return ${project_namespace}::err::EN_SUCCESS;
}

inline static rpc::telemetry::tracer::span_ptr_type __setup_tracer(rpc::context &__child_ctx,
                                                                   rpc::telemetry::tracer &__tracer,
                                                                   atfw::orbit::OrbitRpcMessageHead &head,
                                                                   atfw::util::nostd::string_view rpc_full_name,
                                                                   rpc::telemetry::trace_attributes_type attributes) {
  rpc::telemetry::trace_start_option __trace_option;
  __trace_option.dispatcher = std::static_pointer_cast<dispatcher_implement>(orbit_msg_dispatcher::me());
  __trace_option.is_remote = true;
  __trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_CLIENT;
  __trace_option.attributes = attributes;

  __child_ctx.setup_tracer(__tracer, rpc::context::string_view{rpc_full_name.data(), rpc_full_name.size()},
                           std::move(__trace_option));
  if (__tracer.is_recording()) {
    auto __child_trace_span = __child_ctx.get_trace_span();
    if (__child_trace_span) {
      auto trace_span_head = head.mutable_rpc_trace();
      if (trace_span_head) {
        auto trace_context = __child_trace_span->GetContext();
        rpc::telemetry::tracer::trace_id_span trace_id = trace_context.trace_id().Id();
        rpc::telemetry::tracer::span_id_span span_id = trace_context.span_id().Id();

        trace_span_head->mutable_trace_id()->assign(reinterpret_cast<const char *>(trace_id.data()), trace_id.size());
        trace_span_head->mutable_span_id()->assign(reinterpret_cast<const char *>(span_id.data()), span_id.size());
        trace_span_head->set_kind(__trace_option.kind);
        trace_span_head->set_name(static_cast<std::string>(rpc_full_name));
      }
    }

    return __child_trace_span;
  }

  auto trace_span_head = head.mutable_rpc_trace();
  if (trace_span_head) {
    trace_span_head->set_dynamic_ignore(true);
  }

  return rpc::telemetry::tracer::span_ptr_type();
}

inline static int __setup_rpc_stream_header(atfw::orbit::OrbitRpcMessageHead &head, atfw::util::nostd::string_view rpc_full_name,
                                            atfw::util::nostd::string_view type_full_name,
                                            atfw::util::nostd::string_view callee_name) {
  head.set_timestamp(util::time::time_utility::get_now());

  atframework::RpcStreamMeta *stream_meta = head.mutable_rpc_stream();
  if (nullptr == stream_meta) {
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }

  stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  stream_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  stream_meta->set_callee(static_cast<std::string>(callee_name));
  stream_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  stream_meta->set_type_url(type_full_name.data(), type_full_name.size());
  stream_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  stream_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());
  return ${project_namespace}::err::EN_SUCCESS;
}

inline static int __setup_rpc_request_header(atfw::orbit::OrbitRpcMessageHead &head, task_type_trait::id_type task_id,
                                             atfw::util::nostd::string_view rpc_full_name,
                                             atfw::util::nostd::string_view type_full_name,
                                             atfw::util::nostd::string_view callee_name) {
  head.set_source_task_id(task_id);
  head.set_timestamp(util::time::time_utility::get_now());

  atframework::RpcRequestMeta *request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return ${project_namespace}::err::EN_SYS_MALLOC;
  }

  request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  request_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  request_meta->set_callee(static_cast<std::string>(callee_name));
  request_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  request_meta->set_type_url(type_full_name.data(), type_full_name.size());
  request_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  request_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());
  return ${project_namespace}::err::EN_SUCCESS;
}

template <class TResponseBody>
inline static rpc::result_code_type __rpc_wait_and_unpack_response(rpc::context &__ctx, TResponseBody &rsp_body,
                                                                   atfw::util::nostd::string_view rpc_full_name,
                                                                   atfw::util::nostd::string_view type_full_name,
                                                                   dispatcher_await_options &await_options) {
  atfw::orbit::OrbitRpcMessage *rsp_msg_ptr = __ctx.create<atfw::orbit::OrbitRpcMessage>();
  if (nullptr == rsp_msg_ptr) {
    FWLOGERROR("rpc {} create response message failed", rpc_full_name);
    RPC_RETURN_CODE(${project_namespace}::err::EN_SYS_MALLOC);
  }

  atfw::orbit::OrbitRpcMessage &rsp_msg = *rsp_msg_ptr;
  rpc::result_code_type::value_type res = RPC_AWAIT_CODE_RESULT(rpc::custom_wait(
      __ctx, reinterpret_cast<const void *>(orbit_msg_dispatcher::me()->get_instance_ident()), await_options,
      [](const dispatcher_resume_data_type *resume_data, atfw::orbit::OrbitRpcMessage &stack_rsp) {
        if (nullptr == resume_data || nullptr == resume_data->message.msg_addr) {
          return;
        }

        stack_rsp.Swap(reinterpret_cast<atfw::orbit::OrbitRpcMessage *>(resume_data->message.msg_addr));
      },
      rsp_msg));

  if (res == 0) {
    res = rsp_msg.head().error_code();
  }

  if (rsp_msg.has_head() && rsp_msg.head().has_rpc_response()) {
    if (__to_string_view(rsp_msg.head().rpc_response().type_url()) != type_full_name) {
      if (res >= 0 || !rsp_msg.head().rpc_response().type_url().empty()) {
        FWLOGERROR("rpc {} expect response message {}, but got {}", rpc_full_name, type_full_name,
                   rsp_msg.head().rpc_response().type_url());
      }
    } else if (!rsp_msg.body_bin().empty()) {
      res = __unpack_rpc_body(rsp_body, rsp_msg.body_bin(), rpc_full_name, type_full_name);
      if (res < 0) {
        RPC_RETURN_CODE(res);
      }
    }
  }

  if (rsp_msg.has_head() && rsp_msg.head().error_code() != 0) {
    RPC_RETURN_CODE(rsp_msg.head().error_code());
  }

  RPC_RETURN_CODE(res);
}
}  // namespace

% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
% for rpc in rpcs.values():
<%
    rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
    rpc_allow_no_wait = False
    if not rpc_is_stream_mode:
        rpc_allow_no_wait = rpc.get_extension_field('rpc_options', lambda x: x.allow_no_wait, False)
    rpc_params = ['context& __ctx', 'const std::string &client_id', '{0} &req_body'.format(rpc.get_request().get_cpp_class_name())]
    if not rpc_is_stream_mode:
        rpc_params.append('{0} &rsp_body'.format(rpc.get_response().get_cpp_class_name()))
        if rpc_allow_no_wait:
            rpc_params.append('bool __no_wait')
            rpc_params.append('dispatcher_await_options* __wait_later')
        rpc_return_type = 'rpc::result_code_type'
        rpc_return_sentense = rpc_return_result_code_sentense
    else:
        rpc_return_type = 'rpc::always_ready_code_type'
        rpc_return_sentense = rpc_return_always_ready_code_sentense
%>
// ============ ${rpc.get_full_name()} ============
namespace packer {
${rpc_dllexport_decl} gsl::string_view get_full_name_of_${rpc.get_name()}() {
  return "${rpc.get_full_name()}";
}

${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string &output, const ${rpc.get_request().get_cpp_class_name()} &input) {
  return ${project_namespace}::err::EN_SUCCESS ==
         __pack_rpc_body(input, &output, "${rpc.get_full_name()}",
                         __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()));
}

${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string &input, ${rpc.get_request().get_cpp_class_name()} &output) {
  return ${project_namespace}::err::EN_SUCCESS ==
         __unpack_rpc_body(output, input, "${rpc.get_full_name()}",
                           __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()));
}

% if not rpc_is_stream_mode:
${rpc_dllexport_decl} bool pack_${rpc.get_name()}(std::string &output, const ${rpc.get_response().get_cpp_class_name()} &input) {
  return ${project_namespace}::err::EN_SUCCESS ==
         __pack_rpc_body(input, &output, "${rpc.get_full_name()}",
                         __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
}

${rpc_dllexport_decl} bool unpack_${rpc.get_name()}(const std::string &input, ${rpc.get_response().get_cpp_class_name()} &output) {
  return ${project_namespace}::err::EN_SUCCESS ==
         __unpack_rpc_body(output, input, "${rpc.get_full_name()}",
                           __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()));
}
% endif
}  // namespace packer

${rpc_dllexport_decl} ${rpc_return_type} ${rpc.get_name()}(${', '.join(rpc_params)}) {
  if (client_id.empty()) {
    ${rpc_return_sentense(project_namespace + '::err::EN_SYS_PARAM')}
  }

% if not rpc_is_stream_mode:
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("[ORBIT_RPC] rpc {} must be called in a task", "${rpc.get_full_name()}")
% endif

  atfw::orbit::OrbitRpcMessage *req_msg_ptr = __ctx.create<atfw::orbit::OrbitRpcMessage>();
  if (nullptr == req_msg_ptr) {
    FWLOGERROR("[ORBIT_RPC] rpc {} create request message failed", "${rpc.get_full_name()}");
    ${rpc_return_sentense(project_namespace + '::err::EN_SYS_MALLOC')}
  }

  rpc::result_code_type::value_type res = ${project_namespace}::err::EN_SUCCESS;
  atfw::orbit::OrbitRpcMessage &req_msg = *req_msg_ptr;
% if rpc_allow_no_wait:
  if (__no_wait) {
    res = __setup_rpc_stream_header(*req_msg.mutable_head(), "${rpc.get_full_name()}",
                                    __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()),
                                    "${service.get_full_name()}");
  } else {
    res = __setup_rpc_request_header(*req_msg.mutable_head(), __ctx.get_task_context().task_id,
                                     "${rpc.get_full_name()}",
                                     __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()),
                                     "${service.get_full_name()}");
  }
% elif rpc_is_stream_mode:
  res = __setup_rpc_stream_header(*req_msg.mutable_head(), "${rpc.get_full_name()}",
                                  __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()),
                                  "${service.get_full_name()}");
% else:
  res = __setup_rpc_request_header(*req_msg.mutable_head(), __ctx.get_task_context().task_id,
                                   "${rpc.get_full_name()}",
                                   __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()),
                                   "${service.get_full_name()}");
% endif
  if (res < 0) {
    ${rpc_return_sentense('res')}
  }

  res = __pack_rpc_body(req_body, req_msg.mutable_body_bin(), "${rpc.get_full_name()}",
                        __to_string_view(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name()));
  if (res < 0) {
    ${rpc_return_sentense('res')}
  }

  rpc::context __child_ctx(__ctx);
  rpc::telemetry::tracer __tracer;
  rpc::telemetry::trace_attribute_pair_type __trace_attributes[] = {
      {opentelemetry::semconv::rpc::kRpcSystemName, "atrpc.orbit"},
      {opentelemetry::semconv::rpc::kRpcMethod, "${rpc.get_full_name()}"}};
  __setup_tracer(__child_ctx, __tracer, *req_msg.mutable_head(), "${rpc.get_full_name()}", __trace_attributes);

% if rpc_is_stream_mode:
  res = orbit_msg_dispatcher::me()->send_to_client_no_wait(__child_ctx, client_id, req_msg);
  if (res < 0) {
    FWLOGERROR("[ORBIT_RPC] rpc {} call failed, res: {}({})", "${rpc.get_full_name()}", res,
               protobuf_mini_dumper_get_error_msg(res));
  }
  ${rpc_return_sentense('__tracer.finish({res, __trace_attributes})')}
% else:
  res = RPC_AWAIT_CODE_RESULT(orbit_msg_dispatcher::me()->send_to_client(__child_ctx, client_id, req_msg));
  do {
    dispatcher_await_options await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = req_msg.head().sequence();
    const google::protobuf::MethodDescriptor *method = ${service.get_cpp_class_name()}::descriptor()->FindMethodByName("${rpc.get_name()}");
    if (nullptr != method && method->options().HasExtension(atframework::rpc_options)) {
      await_options.timeout = rpc::make_duration_or_default(
          method->options().GetExtension(atframework::rpc_options).timeout(), std::chrono::seconds{5});
    } else {
      await_options.timeout = std::chrono::seconds{5};
    }

%   if rpc_allow_no_wait:
    if (__no_wait) {
      break;
    }
    if (nullptr != __wait_later) {
      *__wait_later = await_options;
      break;
    }
%   endif
    if (res < 0) {
      break;
    }

    res = RPC_AWAIT_CODE_RESULT(__rpc_wait_and_unpack_response(
        __ctx, rsp_body, "${rpc.get_full_name()}",
        __to_string_view(${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()), await_options));
  } while (false);

  if (res < 0) {
    FWLOGERROR("[ORBIT_RPC] rpc {} call failed, res: {}({})", "${rpc.get_full_name()}", res,
               protobuf_mini_dumper_get_error_msg(res));
  }

  ${rpc_return_sentense('__tracer.finish({res, __trace_attributes})')}
% endif
}
% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}  // namespace rpc