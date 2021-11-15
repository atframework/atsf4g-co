// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-01 11:05:42

#ifndef GENERATED_ACTION_TASK_ACTION_SET_SERVER_TIME_H
#define GENERATED_ACTION_TASK_ACTION_SET_SERVER_TIME_H

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_set_server_time
    : public task_action_ss_rpc_base<PROJECT_SERVER_FRAME_NAMESPACE_ID::SSGlobalLogicSetServerTimeSync,
                                     google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_SERVER_FRAME_NAMESPACE_ID::SSGlobalLogicSetServerTimeSync,
                                            google::protobuf::Empty>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  task_action_set_server_time(dispatcher_start_data_t&& param);
  ~task_action_set_server_time();

  const char* name() const override;

  int operator()() override;

  int on_success() override;
  int on_failed() override;
};

#endif  // GENERATED_TASK_ACTION_SET_SERVER_TIME_H
