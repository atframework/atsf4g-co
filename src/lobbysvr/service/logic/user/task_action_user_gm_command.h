// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-12 14:10:41

#pragma once

#include <config/compile_optimize.h>

#include <dispatcher/task_action_cs_req_base.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on



class task_action_user_gm_command : public task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSUserGMCommandReq,
                                                                   PROJECT_NAMESPACE_ID::SCUserGMCommandRsp> {
 public:
  using base_type =
      task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSUserGMCommandReq, PROJECT_NAMESPACE_ID::SCUserGMCommandRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  explicit task_action_user_gm_command(dispatcher_start_data_type&& param);
  ~task_action_user_gm_command() override;

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
