/**
 * @brief Created by owent with generate-for-pb.py at 2020-07-10 21:34:16
 */

#ifndef GENERATED_ACTION_TASK_ACTION_ROUTER_UPDATE_SYNC_H
#define GENERATED_ACTION_TASK_ACTION_ROUTER_UPDATE_SYNC_H

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_router_update_sync
    : public task_action_ss_rpc_base<PROJECT_SERVER_FRAME_NAMESPACE_ID::SSRouterUpdateSync, google::protobuf::Empty> {
 public:
  using base_type =
      task_action_ss_rpc_base<PROJECT_SERVER_FRAME_NAMESPACE_ID::SSRouterUpdateSync, google::protobuf::Empty>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  task_action_router_update_sync(dispatcher_start_data_t&& param);
  ~task_action_router_update_sync();

  bool is_stream_rpc() const override;

  int operator()() override;

  int on_success() override;
  int on_failed() override;
};

#endif  // GENERATED_TASK_ACTION_ROUTER_UPDATE_SYNC_H
