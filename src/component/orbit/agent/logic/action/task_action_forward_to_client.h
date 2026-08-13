// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-04-29 11:47:42

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/agent_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>



class task_action_forward_to_client
    : public task_action_ss_rpc_base<atfw::orbit::CTAForwardToClientReq, atfw::orbit::ATCForwardToClientRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atfw::orbit::CTAForwardToClientReq, atfw::orbit::ATCForwardToClientRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_forward_to_client(dispatcher_start_data_type&& param);
  ~task_action_forward_to_client();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
