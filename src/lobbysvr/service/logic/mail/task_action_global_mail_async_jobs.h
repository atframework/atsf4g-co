// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#pragma once

#include <std/explicit_declare.h>

#include <dispatcher/task_action_no_req_base.h>

#include <chrono>

class player;
class router_player_cache;

class task_action_global_mail_async_jobs : public task_action_no_req_base {
 public:
  using task_action_no_req_base::ctor_param_t;

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_global_mail_async_jobs(ctor_param_t&& param);
  ~task_action_global_mail_async_jobs();

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

  inline const ctor_param_t& get_ctor_param() const { return param_; }

 private:
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type fetch_mail_contents();
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type cleanup_removed_mails();

 private:
  ctor_param_t param_;
  size_t fetch_mail_number_;
  size_t remove_mail_number_;
  std::chrono::system_clock::time_point fetch_timepoint_;
};
