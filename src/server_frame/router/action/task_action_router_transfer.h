﻿/**
 * @brief Created by owent with generate-for-pb.py at 2020-07-10 21:34:16
 */

#ifndef GENERATED_ACTION_TASK_ACTION_ROUTER_TRANSFER_H
#define GENERATED_ACTION_TASK_ACTION_ROUTER_TRANSFER_H

#pragma once

#include <dispatcher/task_action_ss_req_base.h>

class task_action_router_transfer : public task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSRouterTransferReq,
                                                                   PROJECT_NAMESPACE_ID::SSRouterTransferRsp> {
 public:
  using base_type =
      task_action_ss_rpc_base<PROJECT_NAMESPACE_ID::SSRouterTransferReq, PROJECT_NAMESPACE_ID::SSRouterTransferRsp>;
  using message_type = base_type::message_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_router_transfer(dispatcher_start_data_type&& param);
  ~task_action_router_transfer();

  bool is_stream_rpc() const noexcept override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};

#endif  // GENERATED_TASK_ACTION_ROUTER_TRANSFER_H
