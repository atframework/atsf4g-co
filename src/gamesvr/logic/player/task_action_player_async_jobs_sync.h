/**
 * @brief Created by owent with generate-for-pb.py at 2021-10-30 00:43:04
 */

#ifndef GENERATED_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_SYNC_H
#define GENERATED_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_SYNC_H

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_player_async_jobs_sync
    : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync, google::protobuf::Empty> {
 public:
  using base_type = task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSPlayerAsyncJobsSync, google::protobuf::Empty>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_player_async_jobs_sync(dispatcher_start_data_type&& param);
  ~task_action_player_async_jobs_sync();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};

#endif  // GENERATED_TASK_ACTION_PLAYER_ASYNC_JOBS_SYNC_H
