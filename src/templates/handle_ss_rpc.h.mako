## -*- coding: utf-8 -*-
<%!
import time
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

#ifndef GENERATED_HANDLE_SS_RPC_${service.get_identify_upper_rule(output_render_path)}
#define GENERATED_HANDLE_SS_RPC_${service.get_identify_upper_rule(output_render_path)}

#pragma once

class dispatcher_implement;

namespace handle {
% for ns in service.get_cpp_namespace_begin(module_name, ''):
${ns}
% endfor
int register_handles_for_${service.get_name_lower_rule()}();
% for ns in service.get_cpp_namespace_end(module_name, ''):
${ns}
% endfor
}

#endif