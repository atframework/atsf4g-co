// Copyright 2021 atframework
// Created by owent on 2018-05-09.
//

#pragma once

#include <dispatcher/task_action_no_req_base.h>

#include <dispatcher/task_manager.h>

#include <memory>

class player;

class task_action_player_async_jobs : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::shared_ptr<player> user;
    task_type_trait::task_type after;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_player_async_jobs(ctor_param_t&& param);
  ~task_action_player_async_jobs();

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  ctor_param_t param_;
};
