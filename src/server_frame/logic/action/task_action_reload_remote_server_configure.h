// Copyright 2021 atframework
// @brief Created by owent on 2021-05-21 15:19:58

#pragma once

#include <dispatcher/task_action_no_req_base.h>

class task_action_reload_remote_server_configure : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {};

 public:
  using task_action_no_req_base::operator();

 public:
  task_action_reload_remote_server_configure(ctor_param_t&& param);
  ~task_action_reload_remote_server_configure();

  const char* name() const override;

  int operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  ctor_param_t param_;
};
