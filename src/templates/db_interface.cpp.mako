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
%>

#include "${include_cpp}"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <hiredis_happ.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

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