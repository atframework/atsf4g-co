// Copyright 2021 atframework
// Created by owent on 2016-11-14.
//

#pragma once

#include <memory>

#include "dispatcher/actor_action_base.h"

class actor_action_no_req_base : public actor_action_base {
  using base_type = actor_action_base;

  struct ctor_param_t {
    rpc::context *caller_context;

    ctor_param_t();
  };

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  actor_action_no_req_base();
  explicit actor_action_no_req_base(const ctor_param_t &param);
  ~actor_action_no_req_base();

  std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  const char *get_type_name() const override;

 protected:
  void send_response() override;
};
