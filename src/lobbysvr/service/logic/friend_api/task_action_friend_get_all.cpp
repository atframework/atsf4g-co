// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-28 17:11:31

#include "logic/friend_api/task_action_friend_get_all.h"

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
#include "logic/friend_api/user_friend_api_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_get_all::task_action_friend_get_all(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_get_all::~task_action_friend_get_all() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_friend_get_all::name() const {
  return "task_action_friend_get_all";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_get_all::result_type
task_action_friend_get_all::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto res = RPC_AWAIT_CODE_RESULT(user_inst->get_user_friend_api_manager().pull_friend_data(get_shared_context()));
  if (res < 0) {
    FCTXLOGERROR(get_shared_context(), "{} try to pull frien data failed. res: {}({})", *user_inst, res,
                 protobuf_mini_dumper_get_error_msg(res));
    if (PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT == res || PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT == res) {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT);
    } else {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    }
    TASK_ACTION_RETURN_CODE(res);
  }
  user_inst->get_user_friend_api_manager().cleanup_friend_data(get_shared_context());

  user_inst->get_user_friend_api_manager().dump_stats(*rsp_body.mutable_stats());

  rsp_body.mutable_daily_send_list()->Reserve(
      static_cast<int>(user_inst->get_user_friend_api_manager().get_today_send_gift().size()));
  for (const auto& send_gift_data : user_inst->get_user_friend_api_manager().get_today_send_gift()) {
    protobuf_copy_message(*rsp_body.add_daily_send_list(), send_gift_data.second);
  }

  rsp_body.mutable_daily_receive_list()->Reserve(
      static_cast<int>(user_inst->get_user_friend_api_manager().get_today_receive_gift().size()));
  for (const auto& send_gift_data : user_inst->get_user_friend_api_manager().get_today_receive_gift()) {
    protobuf_copy_message(*rsp_body.add_daily_receive_list(), send_gift_data.second);
  }

  rsp_body.mutable_friend_list()->Reserve(
      static_cast<int>(user_inst->get_user_friend_api_manager().get_all_friend_cache().size()));
  for (const auto& friend_data : user_inst->get_user_friend_api_manager().get_all_friend_cache()) {
    protobuf_copy_message(*rsp_body.add_friend_list(), friend_data.second);
  }

  rsp_body.mutable_inviter_list()->Reserve(
      static_cast<int>(user_inst->get_user_friend_api_manager().get_all_inviter_cache().size()));
  for (const auto& invite_data : user_inst->get_user_friend_api_manager().get_all_inviter_cache()) {
    protobuf_copy_message(*rsp_body.add_inviter_list(), invite_data.second);
  }

  rsp_body.mutable_invitee_list()->Reserve(
      static_cast<int>(user_inst->get_user_friend_api_manager().get_all_invitee_cache().size()));
  for (const auto& invite_data : user_inst->get_user_friend_api_manager().get_all_invitee_cache()) {
    protobuf_copy_message(*rsp_body.add_invitee_list(), invite_data.second);
  }

  rsp_body.mutable_gift_list()->Reserve(
      static_cast<int>(user_inst->get_user_friend_api_manager().get_all_gift_cache().size()));
  for (const auto& gift_data : user_inst->get_user_friend_api_manager().get_all_gift_cache()) {
    protobuf_copy_message(*rsp_body.add_gift_list(), gift_data.second);
  }

  rsp_body.mutable_sns_friend_list()->Reserve(
      static_cast<int>(user_inst->get_user_friend_api_manager().get_all_sns_friend_cache().size()));
  for (const auto& sns_friend_data : user_inst->get_user_friend_api_manager().get_all_sns_friend_cache()) {
    protobuf_copy_message(*rsp_body.add_sns_friend_list(), sns_friend_data.second);
  }

  // TODO(any): ================ 社交分享类数据 ================

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_get_all::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_get_all::on_failed() { return get_result(); }
