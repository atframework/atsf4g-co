// Copyright 2021 atframework
// Created by owent on 2019-06-20.
//

#ifndef ROUTER_ACTION_TASK_ACTION_ROUTER_CLOSE_MANAGER_SET_H
#define ROUTER_ACTION_TASK_ACTION_ROUTER_CLOSE_MANAGER_SET_H

#pragma once

#include <dispatcher/task_action_no_req_base.h>

#include <memory>
#include <vector>

#include "rpc/rpc_common_types.h"

class router_object_base;

class task_action_router_close_manager_set : public task_action_no_req_base {
 public:
  using router_object_ptr_t = std::shared_ptr<router_object_base>;
  using pending_list_t = std::vector<router_object_ptr_t>;
  using pending_list_ptr_t = std::shared_ptr<pending_list_t>;

  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    pending_list_ptr_t pending_list;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_router_close_manager_set(ctor_param_t&& param);
  ~task_action_router_close_manager_set();

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
  int on_timeout() override;

 private:
  rpc::result_code_type save_fallback();

 private:
  ctor_param_t param_;
  struct status_data_t {
    int success_count_;
    int failed_count_;
    size_t current_idx_;
  };
  std::shared_ptr<status_data_t> status_data_;
};

#endif  // ROUTER_ACTION_TASK_ACTION_ROUTER_CLOSE_MANAGER_SET_H
