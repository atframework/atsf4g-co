// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:57:03

#pragma once

#include <dispatcher/task_action_cs_req_base.h>

class player;

class task_action_login
    : public task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSLoginReq, PROJECT_NAMESPACE_ID::SCLoginRsp> {
 public:
  using base_type = task_action_cs_rpc_base<PROJECT_NAMESPACE_ID::CSLoginReq, PROJECT_NAMESPACE_ID::SCLoginRsp>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = base_type::msg_ref_type;
  using msg_cref_type = base_type::msg_cref_type;
  using rpc_request_type = base_type::rpc_request_type;
  using rpc_response_type = base_type::rpc_response_type;

  using task_action_cs_req_base::operator();

 public:
  explicit task_action_login(dispatcher_start_data_t&& param);
  ~task_action_login();

  const char* name() const override;

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  rpc::result_code_type replace_session(std::shared_ptr<player> user);
  rpc::result_code_type await_io_task(rpc::context& ctx, std::shared_ptr<player> user);

 private:
  bool is_new_player_;
};
