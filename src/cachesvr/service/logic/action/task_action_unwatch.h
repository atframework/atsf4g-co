// Copyright 2026 atframework
// Created by owent with generate-for-pb.py at 2020-12-15 16:29:08
//

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

#include <rpc/cache/cache_algorithm.h>

class task_action_unwatch
    : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSCacheUnwatchSync, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSCacheUnwatchSync, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  task_action_unwatch(dispatcher_start_data_type&& param);
  ~task_action_unwatch();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
