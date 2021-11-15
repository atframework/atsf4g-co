// Copyright 2021 atframework
// Created by owent on 2016/10/6.
//

#ifndef LOGIC_ACTION_TASK_ACTION_PLAYER_INFO_GET_H
#define LOGIC_ACTION_TASK_ACTION_PLAYER_INFO_GET_H

#pragma once

#include <dispatcher/actor_action_cs_req_base.h>

class task_action_player_info_get : public actor_action_cs_req_base {
 public:
  using msg_type = actor_action_cs_req_base::msg_type;
  using msg_ref_type = actor_action_cs_req_base::msg_ref_type;
  using msg_cref_type = actor_action_cs_req_base::msg_cref_type;

  using actor_action_cs_req_base::operator();

 public:
  task_action_player_info_get(dispatcher_start_data_t &&param);
  ~task_action_player_info_get();

  virtual int operator()();

  virtual int on_success();
  virtual int on_failed();

 private:
  PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg &get_rsp();
  PROJECT_SERVER_FRAME_NAMESPACE_ID::CSMsg *rsp_;
};

#endif  //_LOGIC_ACTION_TASK_ACTION_PLAYER_INFO_GET_H
