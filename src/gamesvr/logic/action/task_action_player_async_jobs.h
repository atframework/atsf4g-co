// Copyright 2021 atframework
// Created by owent on 2018/05/09.
//

#ifndef ROUTER_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_H
#define ROUTER_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_H

#pragma once

#include <dispatcher/task_action_no_req_base.h>

#include <dispatcher/task_manager.h>

#include <memory>

class player;

class task_action_player_async_jobs : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::shared_ptr<player> user;
    task_manager::task_ptr_t after;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  task_action_player_async_jobs(ctor_param_t&& param);
  ~task_action_player_async_jobs();

  virtual int operator()();

  virtual int on_success();
  virtual int on_failed();

 private:
  ctor_param_t param_;
};

#endif  // ROUTER_ACTION_TASK_ACTION_PLAYER_ASYNC_JOBS_H
