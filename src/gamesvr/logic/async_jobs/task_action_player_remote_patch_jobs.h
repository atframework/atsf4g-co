// Copyright 2021 atframework
// Created by owent on 2018-05-19.
//

#pragma once

#include <memory/rc_ptr.h>
#include <std/smart_ptr.h>

#include <dispatcher/task_action_no_req_base.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
  using async_job_ptr_type = util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data>;
  using sync_callback_type = int32_t (*)(task_action_player_remote_patch_jobs&, player&, int32_t, async_job_ptr_type);
  using async_callback_type = rpc::result_code_type (*)(rpc::context&, player&, int32_t, async_job_ptr_type);

  void register_callbacks(std::unordered_map<int32_t, sync_callback_type>& sync_callbacks,
                          std::unordered_map<int32_t, async_callback_type>& async_callbacks);

  int32_t do_job(int32_t job_type, const async_job_ptr_type& job_data);

  void append_sub_task(task_type_trait::task_type task_inst);

 private:
  ctor_param_t param_;
  bool need_restart_;
  bool is_writable_;
  size_t patched_job_number_;

  std::vector<task_type_trait::task_type> sub_tasks_;
};
