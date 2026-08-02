// Copyright 2026 atframework
// Created by owent on 2016/08/02.
//

#include "rpc/internal/rpc_template_ss_message.h"

#include <time/time_utility.h>

#include <config/logic_config.h>

#include <string>

namespace rpc {
namespace internal {
SERVER_FRAME_API int setup_ss_rpc_request_header(atframework::SSMsgHead& head, task_type_trait::id_type task_id,
                                                 atfw::util::nostd::string_view service_full_name,
                                                 atfw::util::nostd::string_view rpc_full_name,
                                                 atfw::util::nostd::string_view type_full_name) {
  head.set_source_task_id(task_id);
  atframework::RpcRequestMeta* request_meta = head.mutable_rpc_request();
  if (nullptr == request_meta) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }
  request_meta->set_version(logic_config::me()->get_atframework_settings().rpc_version());
  request_meta->set_caller(static_cast<std::string>(logic_config::me()->get_local_server_name()));
  request_meta->set_callee(static_cast<std::string>(service_full_name));
  request_meta->set_rpc_name(static_cast<std::string>(rpc_full_name));
  request_meta->set_type_url(type_full_name.data(), type_full_name.size());
  request_meta->mutable_caller_timestamp()->set_seconds(util::time::time_utility::get_sys_now());
  request_meta->mutable_caller_timestamp()->set_nanos(util::time::time_utility::get_now_nanos());

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}
}  // namespace internal
}  // namespace rpc
