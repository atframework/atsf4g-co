// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-11-06 16:17:06

#include "logic/item/task_action_item_move.h"

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
#include <vector>

#include "data/user.h"

#include "logic/item/user_item_container_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_item_move::task_action_item_move(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_item_move::~task_action_item_move() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_item_move::name() const {
  return "task_action_item_move";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_item_move::result_type task_action_item_move::operator()() {
  rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (req_body.requests().empty()) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  std::vector<item_algorithm::ItemContainerGroupMoveRequest> move_request;
  move_request.reserve(req_body.requests_size());
  for (auto& req : *req_body.mutable_requests()) {
    item_algorithm::ItemContainerGroupMoveRequest move_req;
    move_req.source_item_basic.Swap(req.mutable_source_item_basic());
    move_req.target_position.Swap(req.mutable_target_position());
    move_request.push_back(std::move(move_req));
  }

  auto& item_manager = user_inst->get_user_item_container_manager();
  auto check_result = item_manager.check_move(std::move(move_request), item_algorithm::ItemOperationSource{});
  if (check_result.get_error_code() != 0) {
    set_response_code(check_result.get_error_code());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto move_result = item_manager.move(check_result);
  set_response_code(move_result.error_code);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_item_move::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_item_move::on_failed() { return get_result(); }
