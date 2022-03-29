// Copyright 2022 atframework
// @brief Created by owent with generate-for-pb.py at 2022-01-27 10:39:20

#pragma once

#include <config/compiler/protobuf_prefix.h>
#include <pbdesc/distributed_transaction.pb.h>
#include <config/compiler/protobuf_suffix.h>

#include <dispatcher/task_action_ss_req_base.h>

class task_action_query : public task_action_ss_rpc_base<atframework::distributed_system::SSDistributeTransactionQueryReq, atframework::distributed_system::SSDistributeTransactionQueryRsp> {
 public:
  using base_type = task_action_ss_rpc_base<atframework::distributed_system::SSDistributeTransactionQueryReq, atframework::distributed_system::SSDistributeTransactionQueryRsp>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type  = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_ss_req_base::operator();

 public:
  explicit task_action_query(dispatcher_start_data_t&& param);
  ~task_action_query();

  const char *name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

};
