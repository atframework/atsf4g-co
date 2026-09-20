// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-20 16:47:38

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/friend_management_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

#ifndef ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API
#  define ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

class task_action_management_remove_one_inviter
    : public task_action_ss_rpc_base<atframework::friend_api::SSFriendRemoveOneInviterReq,
                                     atframework::friend_api::SSFriendRemoveOneInviterRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::friend_api::SSFriendRemoveOneInviterReq,
                                            atframework::friend_api::SSFriendRemoveOneInviterRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API explicit task_action_management_remove_one_inviter(
      dispatcher_start_data_type&& param);
  ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API ~task_action_management_remove_one_inviter() override;

  ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API const char* name() const override;

  ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API result_type operator()() override;

  ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API int on_success() override;
  ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API int on_failed() override;
};
