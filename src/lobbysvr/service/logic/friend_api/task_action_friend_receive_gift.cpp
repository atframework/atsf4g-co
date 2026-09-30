// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-28 17:11:31

#include "logic/friend_api/task_action_friend_receive_gift.h"

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

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_receive_gift::task_action_friend_receive_gift(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_receive_gift::~task_action_friend_receive_gift() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_friend_receive_gift::name() const {
  return "task_action_friend_receive_gift";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_receive_gift::result_type
task_action_friend_receive_gift::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user_inst->get_user_friend_api_manager().refresh_feature_limit_minute(get_shared_context());

  std::vector<int64_t> gift_ids;
  gift_ids.reserve(static_cast<size_t>(req_body.gift_ids_size()));
  gift_ids.insert(gift_ids.end(), req_body.gift_ids().begin(), req_body.gift_ids().end());

  set_response_code(RPC_AWAIT_CODE_RESULT(user_inst->get_user_friend_api_manager().receive_gifts(
      get_shared_context(), gift_ids, rsp_body.mutable_receive_gifts())));

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_receive_gift::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_receive_gift::on_failed() { return get_result(); }
