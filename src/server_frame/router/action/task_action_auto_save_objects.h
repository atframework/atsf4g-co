// Copyright 2021 atframework
// Created by owent on 2018-05-01.
//

#pragma once

#include <dispatcher/task_action_no_req_base.h>

#include <memory>

class task_action_auto_save_objects : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {};

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_auto_save_objects(ctor_param_t&& param);
  ~task_action_auto_save_objects();

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;
  int on_timeout() override;

 public:
  static bool debug_receive_stop_when_running;

 private:
  static const char* get_action_name(uint32_t);

 private:
  struct status_data_t {
    int success_count_;
    int failed_count_;
    time_t start_timepooint_;
  };
  std::shared_ptr<status_data_t> status_data_;
};
