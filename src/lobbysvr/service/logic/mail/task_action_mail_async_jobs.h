// Copyright 2026 atframework
// Created by owent on 2026-11-02.
//

#pragma once

#include <dispatcher/task_action_no_req_base.h>

#include <cstddef>
#include <memory>

class user;
class router_user_cache;

class task_action_mail_async_jobs : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::shared_ptr<user> user_inst;
    std::chrono::system_clock::duration timeout_duration;
    std::chrono::system_clock::time_point timeout_timepoint;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_mail_async_jobs(ctor_param_t&& param);
  ~task_action_mail_async_jobs();

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type fetch_mail_contents(user& user_inst);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type cleanup_removed_mails(user& user_inst, router_user_cache& cache);

 private:
  ctor_param_t param_;
  bool need_restart_;
  bool is_writable_;
  size_t fetch_mail_number_;
  size_t remove_mail_number_;
  size_t merge_global_mail_number_;
};
