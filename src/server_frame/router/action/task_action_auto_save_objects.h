//
// Created by owent on 2018/05/01.
//

#ifndef ROUTER_ACTION_TASK_ACTION_AUTO_SAVE_OBJECTS_H
#define ROUTER_ACTION_TASK_ACTION_AUTO_SAVE_OBJECTS_H

#pragma once

#include <dispatcher/task_action_no_req_base.h>

class task_action_auto_save_objects : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {};

 public:
  using task_action_no_req_base::operator();

 public:
  task_action_auto_save_objects(ctor_param_t&& param);
  ~task_action_auto_save_objects();

  int operator()() override;

  int on_success() override;
  int on_failed() override;
  int on_timeout() override;

 public:
  static bool debug_receive_stop_when_running;

 private:
  const char* get_action_name(uint32_t) const;

 private:
  int success_count_;
  int failed_count_;
  time_t start_timepooint_;
};

#endif  //_ROUTER_ACTION_TASK_ACTION_AUTO_SAVE_OBJECTS_H
