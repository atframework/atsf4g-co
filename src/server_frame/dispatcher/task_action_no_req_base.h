// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <memory>

#include "dispatcher/task_action_base.h"

class task_action_no_req_base : public task_action_base {
 public:
  using base_type = task_action_base;
  using result_type = base_type::result_type;

  struct ctor_param_t {
    rpc::context *caller_context;

    ctor_param_t();
  };

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::get_task_id;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  task_action_no_req_base();
  explicit task_action_no_req_base(const ctor_param_t &param);
  ~task_action_no_req_base();

  std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  const char *get_type_name() const override;

 protected:
  void send_response() override;
};
