// Copyright 2021 atframework
// @brief Created by owent on 2021-11-01 13:17:07

#ifndef LOGIC_ACTION_TASK_ACTION_ASYNC_INVOKE_H
#define LOGIC_ACTION_TASK_ACTION_ASYNC_INVOKE_H

#pragma once

#include <dispatcher/task_action_no_req_base.h>

#include <functional>
#include <string>

#include "rpc/rpc_common_types.h"

class task_action_async_invoke : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::string name;
    std::function<rpc::result_code_type(rpc::context &)> callable;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_async_invoke(ctor_param_t &&param);
  ~task_action_async_invoke();

  const char *name() const override;

  result_type operator()() override;

 private:
  ctor_param_t param_;
};

#endif  // LOGIC_ACTION_TASK_ACTION_ASYNC_INVOKE_H
