## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator}, please don't edit it

<%
file = database.get_file(generate_proto_file)
pb_inclue_path = re.sub(r'\.proto$', '.pb.h', generate_proto_file)
index_type_enum = database.get_enum("atframework.database_index_type")
if index_type_enum is None:
    return
%>

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <${pb_inclue_path}>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
// Per-table typed mock helpers below call through the server-frame mock engine bridge. They only
// exist in builds with PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS, never link the rpc-unit-test
// library, and are never referenced by production code.
#  include "rpc/unit_test/mock_engine_bridge.h"
#  include "rpc/db/hash_table.h"
#endif

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#include <nostd/string_view.h>
#include "rpc/db/db_utils.h"
#include "rpc/rpc_shared_message.h"
#include <dispatcher/db_msg_dispatcher.h>

namespace rpc {
class context;

namespace db {

using string_view = ATFRAMEWORK_UTILS_NAMESPACE_ID::nostd::string_view;

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
<%include file="db_rpc_redis.h.mako" args="message_name=message_name,extension=extension,message=message,index_type_enum=index_type_enum,message_full_name=full_name" />
%   endfor

}  // namespace db
}  // namespace rpc