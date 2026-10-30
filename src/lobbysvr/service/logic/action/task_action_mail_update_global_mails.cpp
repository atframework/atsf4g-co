// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-10-30 21:17:11

#include "logic/action/task_action_mail_update_global_mails.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API task_action_mail_update_global_mails::task_action_mail_update_global_mails(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API task_action_mail_update_global_mails::~task_action_mail_update_global_mails() {}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API const char* task_action_mail_update_global_mails::name() const {
  return "task_action_mail_update_global_mails";
}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API task_action_mail_update_global_mails::result_type
task_action_mail_update_global_mails::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API int task_action_mail_update_global_mails::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRSERVICE_API int task_action_mail_update_global_mails::on_failed() { return get_result(); }
