## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
result_clazz_name = service.get_name_lower_rule() + '_result_t'
%>/**
 * @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it
 */

#include <log/log_wrapper.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>

#include <dispatcher/task_action_ss_req_base.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <router/router_manager_set.h>
#include <router/router_manager_base.h>
#include <router/router_player_manager.h>
#include <router/router_object_base.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

#include "${service.get_name_lower_rule()}.h"

namespace rpc {
% for ns in service.get_cpp_namespace_begin(module_name, '    '):
    ${ns}
% endfor
        ${result_clazz_name}::${result_clazz_name}() {}
        ${result_clazz_name}::${result_clazz_name}(int code): result(code) {}
        ${result_clazz_name}::operator int() const LIBCOPP_MACRO_NOEXCEPT {
            if (!result.is_ready()) {
                return 0;
            }

            const int* ret = result.data();
            if (nullptr == ret) {
                return 0;
            }

            return *ret;
        }

        bool ${result_clazz_name}::is_success() const LIBCOPP_MACRO_NOEXCEPT {
            if (!result.is_ready()) {
                return false;
            }

            const int* ret = result.data();
            if (nullptr == ret) {
                return false;
            }

            return *ret >= 0;
        }

        bool ${result_clazz_name}::is_error() const LIBCOPP_MACRO_NOEXCEPT {
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
        rpc_request_meta_pretty_prefix = '    '
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
                FWLOGERROR("rpc {} must be called in a task", "${rpc.get_full_name()}");
                return ${result_clazz_name}(${project_namespace}::err::EN_SYS_RPC_NO_TASK);
            }
%   endif

            ${project_namespace}::SSMsg* req_msg_ptr = __ctx.create<${project_namespace}::SSMsg>();
            if (nullptr == req_msg_ptr) {
                FWLOGERROR("rpc {} create request message failed", "${rpc.get_full_name()}");
                return ${result_clazz_name}(${project_namespace}::err::EN_SYS_MALLOC);
            }

            ${project_namespace}::SSMsg& req_msg = *req_msg_ptr;
            task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_self_bus_id());
%   if rpc_allow_no_wait:
            if (__no_wait) {
%   endif
%   if rpc_is_stream_mode or rpc_allow_no_wait:
            ${rpc_request_meta_pretty_prefix}req_msg.mutable_head()->set_op_type(${project_namespace}::EN_MSG_OP_TYPE_STREAM);
            ${rpc_request_meta_pretty_prefix}atframework::RpcStreamMeta* stream_meta = req_msg.mutable_head()->mutable_rpc_stream();
            ${rpc_request_meta_pretty_prefix}if (nullptr == stream_meta) {
            ${rpc_request_meta_pretty_prefix}    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_MALLOC);
            ${rpc_request_meta_pretty_prefix}}
            ${rpc_request_meta_pretty_prefix}stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
            ${rpc_request_meta_pretty_prefix}stream_meta->set_caller(ss_msg_dispatcher::me()->get_current_service_name());
            ${rpc_request_meta_pretty_prefix}stream_meta->set_callee("${service.get_full_name()}");
            ${rpc_request_meta_pretty_prefix}stream_meta->set_rpc_name("${rpc.get_full_name()}");
            ${rpc_request_meta_pretty_prefix}stream_meta->set_type_url(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
%   endif
%   if rpc_allow_no_wait:
            } else {
%   endif
%   if not rpc_is_stream_mode:
            ${rpc_request_meta_pretty_prefix}req_msg.mutable_head()->set_src_task_id(task->get_id());
            ${rpc_request_meta_pretty_prefix}req_msg.mutable_head()->set_op_type(${project_namespace}::EN_MSG_OP_TYPE_UNARY_REQUEST);
            ${rpc_request_meta_pretty_prefix}atframework::RpcRequestMeta* request_meta = req_msg.mutable_head()->mutable_rpc_request();
            ${rpc_request_meta_pretty_prefix}if (nullptr == request_meta) {
            ${rpc_request_meta_pretty_prefix}    return ${result_clazz_name}(${project_namespace}::err::EN_SYS_MALLOC);
            ${rpc_request_meta_pretty_prefix}}
            ${rpc_request_meta_pretty_prefix}request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
            ${rpc_request_meta_pretty_prefix}request_meta->set_caller(ss_msg_dispatcher::me()->get_current_service_name());
            ${rpc_request_meta_pretty_prefix}request_meta->set_callee("${service.get_full_name()}");
            ${rpc_request_meta_pretty_prefix}request_meta->set_rpc_name("${rpc.get_full_name()}");
            ${rpc_request_meta_pretty_prefix}request_meta->set_type_url(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
%   endif
%   if rpc_allow_no_wait:
            }
%   endif

            if (false == req_body.SerializeToString(req_msg.mutable_body_bin())) {
                FWLOGERROR("rpc {} serialize message {} failed, msg: {}", "${rpc.get_full_name()}",
                    ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name(), 
                    req_body.InitializationErrorString()
                );
                return ${result_clazz_name}(${project_namespace}::err::EN_SYS_PACK);
            } else {
                FWLOGDEBUG("rpc {} serialize message {} success:\n{}", "${rpc.get_full_name()}",
                    ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name(), 
                    protobuf_mini_dumper_get_readable(req_body)
                );
            }

            rpc::context __child_ctx(__ctx);
            rpc::context::tracer __tracer;
            __child_ctx.setup_tracer(__tracer, "${rpc.get_full_name()}");

            if (nullptr != __child_ctx.get_trace_span()) {
                auto trace_span = req_msg.mutable_head()->mutable_rpc_trace();
                if (nullptr != trace_span) {
                    protobuf_copy_message(*trace_span, *__child_ctx.get_trace_span());
                }
            }

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
            router_manager_base* router_manager = router_manager_set::me()->get_manager(type_id);
            if (nullptr == router_manager) {
                FWLOGERROR("rpc {} can not get router manager of type {}", "${rpc.get_full_name()}", type_id);
                return ${result_clazz_name}(__tracer.return_code(${project_namespace}::err::EN_SYS_NOT_SUPPORT));
            }

            uint64_t rpc_sequence = 0;
            int res = router_manager->send_msg(router_key, std::move(req_msg), rpc_sequence);
%   else:
            if (dst_bus_id == 0) {
                return ${result_clazz_name}(__tracer.return_code(${project_namespace}::err::EN_SYS_PARAM));
            }

            int res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
%   endif

%   if rpc_is_stream_mode:
            if (res < 0) {
%     for warning_log_codes in rpc.get_extension_field('rpc_options', lambda x: x.warning_log_response_code, []):
                if (${warning_log_codes} == res) {
                    FWLOGWARNING("rpc {} wait for {} failed, res: {}({})", "${rpc.get_full_name()}",
                        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                        res, protobuf_mini_dumper_get_error_msg(res)
                    );
                    return ${result_clazz_name}(__tracer.return_code(res));
                }
%     endfor
%     for warning_log_codes in rpc.get_extension_field('rpc_options', lambda x: x.info_log_response_code, []):
                if (${warning_log_codes} == res) {
                    FWLOGINFO("rpc {} wait for {} failed, res: {}({})", "${rpc.get_full_name()}",
                        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                        res, protobuf_mini_dumper_get_error_msg(res)
                    );
                    return ${result_clazz_name}(__tracer.return_code(res));
                }
%     endfor
                FWLOGERROR("rpc {} call failed, res: {}({})", "${rpc.get_full_name()}",
                    res, protobuf_mini_dumper_get_error_msg(res)
                );
            }
            return ${result_clazz_name}(__tracer.return_code(res));
%   else:
            do {
%     if not rpc_is_router_api:
                uint64_t rpc_sequence = req_msg.head().sequence();
%     endif
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

                ${project_namespace}::SSMsg* rsp_msg_ptr = __ctx.create<${project_namespace}::SSMsg>();
                if (nullptr == rsp_msg_ptr) {
                    FWLOGERROR("rpc {} create response message failed", "${rpc.get_full_name()}");
                    res = ${result_clazz_name}(__tracer.return_code(${project_namespace}::err::EN_SYS_MALLOC));
                    break;
                }

                ${project_namespace}::SSMsg& rsp_msg = *rsp_msg_ptr;
                res = rpc::wait(rsp_msg, rpc_sequence);
                if (res < 0) {
                    break;
                }

                if (rsp_msg.head().rpc_response().type_url() != ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name()) {
                    FWLOGERROR("rpc {} expect response message {}, but got {}", "${rpc.get_full_name()}",
                        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                        rsp_msg.head().rpc_response().type_url()
                    );
                }

                if (!rsp_msg.body_bin().empty()) {
                    if (false == rsp_body.ParseFromString(rsp_msg.body_bin())) {
                        FWLOGERROR("rpc {} parse message {} for failed, msg: {}", "${rpc.get_full_name()}", 
                            ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                            rsp_body.InitializationErrorString()
                        );

                        res = ${project_namespace}::err::EN_SYS_UNPACK;
                        break;
                    } else {
                        FWLOGDEBUG("rpc {} parse message {} success:\n{}", "${rpc.get_full_name()}", 
                            ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                            protobuf_mini_dumper_get_readable(rsp_body)
                        );
                    }
                }
                res = rsp_msg.head().error_code();
            } while (false);

            if (res < 0) {
%     for warning_log_codes in rpc.get_extension_field('rpc_options', lambda x: x.warning_log_response_code, []):
                if (${warning_log_codes} == res) {
                    FWLOGWARNING("rpc {} wait for {} failed, res: {}({})", "${rpc.get_full_name()}",
                        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                        res, protobuf_mini_dumper_get_error_msg(res)
                    );
                    return ${result_clazz_name}(__tracer.return_code(res));
                }
%     endfor
%     for warning_log_codes in rpc.get_extension_field('rpc_options', lambda x: x.info_log_response_code, []):
                if (${warning_log_codes} == res) {
                    FWLOGINFO("rpc {} wait for {} failed, res: {}({})", "${rpc.get_full_name()}",
                        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                        res, protobuf_mini_dumper_get_error_msg(res)
                    );
                    return ${result_clazz_name}(__tracer.return_code(res));
                }
%     endfor
                FWLOGERROR("rpc {} call failed, res: {}({})", "${rpc.get_full_name()}",
                    res, protobuf_mini_dumper_get_error_msg(res)
                );
            }

            return ${result_clazz_name}(__tracer.return_code(res));
%   endif
        }
% endfor
% for ns in service.get_cpp_namespace_end(module_name, '    '):
    ${ns}
% endfor
}