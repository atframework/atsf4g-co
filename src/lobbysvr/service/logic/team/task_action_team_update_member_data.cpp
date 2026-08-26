// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-26 17:38:31

#include "logic/team/task_action_team_update_member_data.h"

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

#include <data/user.h>

#include <utility>

#include "logic/team/user_team_algorithm.h"
#include "logic/team/user_team_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_update_member_data::task_action_team_update_member_data(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API
task_action_team_update_member_data::~task_action_team_update_member_data() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_team_update_member_data::name() const {
  return "task_action_team_update_member_data";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_update_member_data::result_type
task_action_team_update_member_data::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(req_body.team_key());
  if (!team_ptr) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (!user_team_algorithm::allow_client_update_team_member_shared_data(req_body.module_type(),
                                                                        req_body.module_data_id())) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // TODO(owent): 各类数据更新的操作
  // - ready状态（condition）: 如果更新为false, 队伍的matching必须为false

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_update_member_data::on_success() {
  return get_result();
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_update_member_data::on_failed() {
  return get_result();
}
