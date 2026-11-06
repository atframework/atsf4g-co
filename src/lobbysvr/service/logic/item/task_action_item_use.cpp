// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-11-06 16:17:06

#include "logic/item/task_action_item_use.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>

#include <utility>

#include "data/user.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_item_use::task_action_item_use(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_item_use::~task_action_item_use() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_item_use::name() const {
  return "task_action_item_use";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_item_use::result_type task_action_item_use::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_item_use::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_item_use::on_failed() { return get_result(); }
