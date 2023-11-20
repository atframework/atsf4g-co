// Copyright 2021 atframework
// Created by owent with generate-for-pb.py at 2020-07-10 22:02:19
//

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_player_kickoff : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSPlayerKickOffReq,
                                                                  PROJECT_NAMESPACE_ID::SSPlayerKickOffRsp> {
 public:
  using base_type =
      task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSPlayerKickOffReq, PROJECT_NAMESPACE_ID::SSPlayerKickOffRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_player_kickoff(dispatcher_start_data_type&& param);
  ~task_action_player_kickoff();

  bool is_stream_rpc() const noexcept override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
