## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
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
% for rpc in rpcs.values():
<%
    rpc_is_router_api = rpc.get_extension_field('rpc_options', lambda x: x.router_rpc, False)
    rpc_is_user_rpc = rpc.get_extension_field('rpc_options', lambda x: x.user_rpc, False)
    rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
    rpc_params = []
    if rpc_is_router_api:
        rpc_params.extend(['uint32_t type_id', 'uint32_t zone_id', 'uint64_t object_id'])
        if rpc_is_user_rpc:
            rpc_params.extend(['uint64_t user_id', "const std::string& open_id"])
    else:
        rpc_params.append('uint64_t dst_bus_id')
        if rpc_is_user_rpc:
            rpc_params.extend(['uint32_t zone_id', 'uint64_t user_id', "const std::string& open_id"])
    rpc_params.append('{0} &req_body'.format(rpc.get_request().get_cpp_class_name()))
    if not rpc_is_stream_mode:
        rpc_params.append('{0} &rsp_body'.format(rpc.get_response().get_cpp_class_name()))
%>
        // ============ ${rpc.get_full_name()} ============
        int ${rpc.get_name()}(${', '.join(rpc_params)}) {
%   if rpc_is_router_api:
            if (object_id == 0 || type_id == 0) {
                return ${project_namespace}::err::EN_SYS_PARAM;
            }
%   else:
            if (dst_bus_id == 0) {
                return ${project_namespace}::err::EN_SYS_PARAM;
            }
%   endif

%   if rpc_is_router_api or not rpc_is_stream_mode:
            task_manager::task_t *task = task_manager::task_t::this_task();
            if (!task) {
                FWLOGERROR("rpc {} must be called in a task", "${rpc.get_full_name()}");
                return ${project_namespace}::err::EN_SYS_RPC_NO_TASK;
            }
%   endif

            ${project_namespace}::SSMsg req_msg;
            task_action_ss_req_base::init_msg(req_msg, logic_config::me()->get_self_bus_id());
%   if rpc_is_stream_mode:
            req_msg.mutable_head()->set_op_type(${project_namespace}::EN_MSG_OP_TYPE_STREAM);
            ${project_namespace}::SSMsgRpcStreamMeta* stream_meta = req_msg.mutable_head()->mutable_rpc_stream();
            if (nullptr == stream_meta) {
                return ${project_namespace}::err::EN_SYS_MALLOC;
            }
            stream_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
            stream_meta->set_caller(ss_msg_dispatcher::me()->get_current_service_name());
            stream_meta->set_callee("${service.get_full_name()}");
            stream_meta->set_rpc_name("${rpc.get_full_name()}");
            stream_meta->set_type_url(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
%   else:
            req_msg.mutable_head()->set_src_task_id(task->get_id());
            req_msg.mutable_head()->set_op_type(${project_namespace}::EN_MSG_OP_TYPE_UNARY_REQUEST);
            ${project_namespace}::SSMsgRpcRequestMeta* request_meta = req_msg.mutable_head()->mutable_rpc_request();
            if (nullptr == request_meta) {
                return ${project_namespace}::err::EN_SYS_MALLOC;
            }
            request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
            request_meta->set_caller(ss_msg_dispatcher::me()->get_current_service_name());
            request_meta->set_callee("${service.get_full_name()}");
            request_meta->set_rpc_name("${rpc.get_full_name()}");
            request_meta->set_type_url(${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name());
%   endif

            if (false == req_body.SerializeToString(req_msg.mutable_body_bin())) {
                FWLOGERROR("rpc {} serialize message {} failed, msg: {}", "${rpc.get_full_name()}",
                    ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name(), 
                    req_body.InitializationErrorString()
                );
                return ${project_namespace}::err::EN_SYS_PACK;
            } else {
                FWLOGDEBUG("rpc {} serialize message {} success:\n{}", "${rpc.get_full_name()}",
                    ${rpc.get_request().get_cpp_class_name()}::descriptor()->full_name(), 
                    protobuf_mini_dumper_get_readable(req_body)
                );
            }

%   if rpc_is_user_rpc:
            req_msg.mutable_head()->set_player_user_id(user_id);
            req_msg.mutable_head()->set_player_zone_id(zone_id);
            req_msg.mutable_head()->set_player_open_id(open_id);
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
                return ${project_namespace}::err::EN_SYS_NOT_SUPPORT;
            }

            uint64_t rpc_sequence = 0;
            int res = router_manager->send_msg(router_key, std::move(req_msg), rpc_sequence);
%   else:
            if (dst_bus_id == 0) {
                return ${project_namespace}::err::EN_SYS_PARAM;
            }

            int res = ss_msg_dispatcher::me()->send_to_proc(dst_bus_id, req_msg);
%   endif

%   if rpc_is_stream_mode:
            return res;
%   else:
%     if not rpc_is_router_api:
            uint64_t rpc_sequence = req_msg.head().sequence();
%     endif
            if (res < 0) {
                return res;
            }

            ${project_namespace}::SSMsg rsp_msg;
            res = rpc::wait(rsp_msg, rpc_sequence);
            if (res < 0) {
                FWLOGERROR("rpc {} wait for {} failed, res: {}({})", "${rpc.get_full_name()}",
                    ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                    res, protobuf_mini_dumper_get_error_msg(res)
                );
                return res;
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

                    return ${project_namespace}::err::EN_SYS_UNPACK;
                } else {
                    FWLOGDEBUG("rpc {} parse message {} success:\n{}", "${rpc.get_full_name()}", 
                        ${rpc.get_response().get_cpp_class_name()}::descriptor()->full_name(), 
                        protobuf_mini_dumper_get_readable(rsp_body)
                    );
                }
            }

            return rsp_msg.head().error_code();
%   endif
        }
% endfor
% for ns in service.get_cpp_namespace_end(module_name, '    '):
    ${ns}
% endfor
}