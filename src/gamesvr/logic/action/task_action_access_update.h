// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:27:14

#pragma once

#include <dispatcher/task_action_cs_req_base.h>

class task_action_access_update : public task_action_cs_rpc_base<PROJECT_SERVER_FRAME_NAMESPACE_ID::CSAccessUpdateReq,
                                                                 PROJECT_SERVER_FRAME_NAMESPACE_ID::SCAccessUpdateRsp> {
 public:
  using base_type = task_action_cs_rpc_base<PROJECT_SERVER_FRAME_NAMESPACE_ID::CSAccessUpdateReq,
                                            PROJECT_SERVER_FRAME_NAMESPACE_ID::SCAccessUpdateRsp>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  explicit task_action_access_update(dispatcher_start_data_t&& param);
  ~task_action_access_update();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
};
