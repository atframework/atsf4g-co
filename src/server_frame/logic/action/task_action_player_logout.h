//
// Created by owent on 2016/10/6.
//

#ifndef LOGIC_ACTION_TASK_ACTION_PLAYER_LOGOUT_H
#define LOGIC_ACTION_TASK_ACTION_PLAYER_LOGOUT_H

#pragma once

#include <dispatcher/task_action_no_req_base.h>

class task_action_player_logout : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    uint64_t atgateway_bus_id;
    uint64_t atgateway_session_id;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  task_action_player_logout(ctor_param_t&& param);
  ~task_action_player_logout();

  int operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  ctor_param_t ctor_param_;
};

#endif  //_LOGIC_ACTION_TASK_ACTION_PLAYER_LOGOUT_H
