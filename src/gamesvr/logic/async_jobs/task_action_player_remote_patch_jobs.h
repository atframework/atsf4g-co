// Copyright 2021 atframework
// Created by owent on 2018-05-19.
//

#pragma once

#include <std/smart_ptr.h>

#include <dispatcher/task_action_no_req_base.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

PROJECT_NAMESPACE_BEGIN
class table_user_async_jobs_blob_data;
PROJECT_NAMESPACE_END

class player;

class task_action_player_remote_patch_jobs : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::shared_ptr<player> user;
    time_t timeout_duration;
    time_t timeout_timepoint;
    std::unordered_set<int32_t> async_job_type;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_player_remote_patch_jobs(ctor_param_t&& param);
  ~task_action_player_remote_patch_jobs();

  result_type operator()() override;

  int on_success() override;
  int on_failed() override;

 private:
  using callback_type = int32_t (*)(task_action_player_remote_patch_jobs&, player&, int32_t,
                                    const PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data&);
  void register_callbacks(std::unordered_map<int32_t, callback_type>& callbacks);

  int32_t do_job(int32_t job_type, const PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data& job_data);

 private:
  ctor_param_t param_;
  bool need_restart_;
  bool is_writable_;
  size_t patched_job_number_;
};
