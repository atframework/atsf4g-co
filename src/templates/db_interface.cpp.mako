## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator}, please don't edit it

<%
file = database.get_file(generate_proto_file)
index_type_enum = database.get_enum("atframework.database_index_type")
if index_type_enum is None:
    return

output_render_dir = os.path.dirname(output_render_path)
cpp_include_base_dir = ''
if output_render_dir and not os.path.isabs(output_render_dir):
    cpp_include_base_dir = output_render_dir + '/'
%>

#include "${cpp_include_base_dir}${include_cpp}"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <log/log_wrapper.h>

#include <hiredis_happ.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include "rpc/rpc_context.h"
#include "rpc/db/db_utils.h"
#include "rpc/db/hash_table.h"

namespace rpc {
namespace db {

%	for message_name, message_desc in file.descriptor.message_types_by_name.items():
<%
    package_name = file.get_package()
    full_name = message_desc.name if package_name == '' else f"{package_name}.{message_desc.name}"
    message = database.get_message(full_name)
    if message is None:
        continue
    extension = message.get_extension("atframework.database_table")
    if extension is None:
        continue
%>
<%include file="db_rpc_redis.cpp.mako" args="message_name=message_name,extension=extension,message=message,index_type_enum=index_type_enum" />
%   endfor

}  // namespace db
}  // namespace rpc
