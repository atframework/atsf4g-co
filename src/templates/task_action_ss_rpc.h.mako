## -*- coding: utf-8 -*-
<%!
import time
import os
%><%
task_class_name = os.path.splitext(os.path.basename(output_render_path))[0]
%>/**
 * @brief Created by ${local_vcs_user_name} with ${generator} at ${time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()) }
 */

#ifndef GENERATED_ACTION_${task_class_name.upper()}_H
#define GENERATED_ACTION_${task_class_name.upper()}_H

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class ${task_class_name} : public task_action_ss_rpc_base<${rpc.get_request().get_cpp_class_name()}, ${rpc.get_response().get_cpp_class_name()}> {
public:
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
    using base_type = task_action_ss_rpc_base<${rpc.get_request().get_cpp_class_name()}, ${rpc.get_response().get_cpp_class_name()}>;
    using msg_type = base_type::msg_type;
    using msg_ref_type = base_type::msg_ref_type;
    using msg_cref_type = base_type::msg_cref_type;
    using rpc_request_type  = base_type::rpc_request_type;
    using rpc_response_type = base_type::rpc_response_type;
#else
    typedef task_action_ss_rpc_base<${rpc.get_request().get_cpp_class_name()}, ${rpc.get_response().get_cpp_class_name()}> base_type;
    typedef base_type::msg_type msg_type;
    typedef base_type::msg_ref_type msg_ref_type;
    typedef base_type::msg_cref_type msg_cref_type;
    typedef base_type::rpc_request_type rpc_request_type;
    typedef base_type::rpc_response_type rpc_response_type;
#endif

    using task_action_ss_req_base::operator();

public:
    ${task_class_name}(dispatcher_start_data_t COPP_MACRO_RV_REF param);
    ~${task_class_name}();

    virtual const char *name() const UTIL_CONFIG_OVERRIDE;

    virtual int operator()() UTIL_CONFIG_OVERRIDE;

    virtual int on_success() UTIL_CONFIG_OVERRIDE;
    virtual int on_failed() UTIL_CONFIG_OVERRIDE;

% if rpc.get_extension_field('rpc_options', lambda x: x.router_rpc, False) and rpc.get_extension_field('rpc_options', lambda x: x.router_ignore_offline, False):
    virtual bool is_router_offline_ignored() const UTIL_CONFIG_OVERRIDE;
% endif
};

#endif // GENERATED_${task_class_name.upper()}_H

