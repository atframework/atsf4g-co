// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-10 20:11:30

#pragma once

#include <config/compile_optimize.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <dispatcher/task_action_ss_req_base.h>

class task_action_send_message : public task_action_ss_rpc_base<atframework::team::SSTeamRoomSendMessageReq,
                                                                atframework::team::SSTeamRoomSendMessageRsp> {
 public:
  using base_type =
      task_action_ss_rpc_base<atframework::team::SSTeamRoomSendMessageReq, atframework::team::SSTeamRoomSendMessageRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_send_message(dispatcher_start_data_type&& param);
  ~task_action_send_message() override;

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
