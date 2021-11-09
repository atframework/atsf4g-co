// Copyright 2021 atframework
// Created by owent on 2018/05/19.
//

#pragma once

#include <std/smart_ptr.h>

#include <dispatcher/task_action_no_req_base.h>

class player;

class task_action_player_remote_patch_jobs : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::shared_ptr<player> user;
    time_t timeout_duration;
    time_t timeout_timepoint;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  task_action_player_remote_patch_jobs(ctor_param_t&& param);
  ~task_action_player_remote_patch_jobs();

  virtual int operator()();

  virtual int on_success();
  virtual int on_failed();

 private:
  ctor_param_t param_;
  bool need_restart_;
  bool is_writable_;
  size_t patched_job_number_;
};