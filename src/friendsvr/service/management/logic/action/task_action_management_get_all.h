// Copyright 2026 atframework

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/friend_management_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

class task_action_management_get_all
    : public task_action_ss_rpc_base<atframework::friends::SSFriendManagementGetAllReq,
                                     atframework::friends::SSFriendManagementGetAllRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::friends::SSFriendManagementGetAllReq,
                                            atframework::friends::SSFriendManagementGetAllRsp>;
  using base_type::operator();

  explicit task_action_management_get_all(dispatcher_start_data_type&& param);
  ~task_action_management_get_all() override;

  const char* name() const override;
  result_type operator()() override;
  int on_success() override;
  int on_failed() override;
};
