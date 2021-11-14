## -*- coding: utf-8 -*-
<%!
import time
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
result_clazz_name = service.get_name_lower_rule() + '_result_t'
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#ifndef GENERATED_API_${service.get_identify_upper_rule(output_render_path)}
#define GENERATED_API_${service.get_identify_upper_rule(output_render_path)}

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <libcopp/future/poller.h>

#include <cstddef>
#include <stdint.h>
#include <cstring>
#include <string>

namespace rpc {
class context;
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
struct ${result_clazz_name} {
  ${result_clazz_name}();
  ${result_clazz_name}(int code);

  // Remove this and implement co_yield to get the result in the future
  explicit operator int() const noexcept;

  bool is_success() const noexcept;
  bool is_error() const noexcept;

  copp::future::poller<int> result;
};
% for rpc in rpcs.values():
<%
    rpc_is_router_api = rpc.get_extension_field('rpc_options', lambda x: x.router_rpc, False)
    rpc_is_user_rpc = rpc.get_extension_field('rpc_options', lambda x: x.user_rpc, False)
    rpc_is_stream_mode = rpc.is_request_stream() or rpc.is_response_stream()
    rpc_params = ['context& __ctx']
    rpc_param_docs = ['__ctx          RPC context, you can get it from get_shared_context() of task_action or just create one on stack']
    if rpc_is_router_api:
        rpc_params.extend(['uint32_t type_id', 'uint32_t zone_id', 'uint64_t object_id'])
        rpc_param_docs.extend([
            'type_id        router object type id',
            'zone_id        router object zone id, pass 0 if it has no zone id',
            'object_id      router object instance id'
        ])
    else:
        rpc_params.append('uint64_t dst_bus_id')
        rpc_param_docs.append('dst_bus_id     target server bus id')
        if rpc_is_user_rpc:
            rpc_params.extend(['uint32_t zone_id', 'uint64_t user_id', "const std::string& open_id"])
            rpc_param_docs.extend([
                'zone_id        zone id that will be passsed into header',
                'user_id        user id that will be passsed into header',
                'open_id        open id that will be passsed into header'
            ])
    rpc_params.append('{0} &req_body'.format(rpc.get_request().get_cpp_class_name()))
    rpc_param_docs.append('req_body       request body')
    if not rpc_is_stream_mode:
        rpc_params.append('{0} &rsp_body'.format(rpc.get_response().get_cpp_class_name()))
        rpc_param_docs.append('rsp_body       response body')
        if rpc.get_extension_field('rpc_options', lambda x: x.allow_no_wait, False):
            rpc_params.append('bool __no_wait = false')
            rpc_params.append('uint64_t* __wait_later = nullptr')
            rpc_param_docs.append('__no_wait      set true if not need to wait response')
            rpc_param_docs.append('__wait_later   set not nullptr if caller want to wait this RPC later, and receive rpc sequence to wait here')
%>
// ============ ${rpc.get_full_name()} ============
/**
 * @brief ${rpc.get_extension_field('rpc_options', lambda x: x.api_name, rpc.get_name())}
%   for param_doc in rpc_param_docs:
 * @param ${param_doc.replace('*/', '*')}
%   endfor
%   for desc in rpc.get_extension_field('rpc_options', lambda x: x.descriptions, []):
 * @note  ${desc.replace('*/', '*')}
%   endfor
 * @return 0 or error code
 */
${result_clazz_name} ${rpc.get_name()}(${', '.join(rpc_params)});
% endfor
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}

#endif
