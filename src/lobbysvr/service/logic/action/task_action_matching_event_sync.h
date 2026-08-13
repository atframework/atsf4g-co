// Copyright 2026 atframework
// @brief Created by jijunliang with mako-generator.py at 2026-08-04 17:03:47

#pragma once

#include <config/compile_optimize.h>

#include <dispatcher/task_action_ss_req_base.h>
#include <rpc/matching/matchsvrnotifyservice.atfw.gen.h>



class task_action_matching_event_sync
    : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingEventSync, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSMatchingEventSync, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_matching_event_sync(dispatcher_start_data_type&& param);
  ~task_action_matching_event_sync() override;

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
