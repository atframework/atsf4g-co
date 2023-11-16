// Copyright 2021 atframework
// Created by owent on 2016/10/6.
//

#pragma once

#include <dispatcher/task_action_no_req_base.h>

class task_action_player_logout : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    uint64_t atgateway_node_id;
    uint64_t atgateway_session_id;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_player_logout(ctor_param_t&& param);
  ~task_action_player_logout();

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  ctor_param_t ctor_param_;
};
