// Copyright 2021 atframework
// Created by owent

#include "logic/async_jobs/user_async_jobs_manager.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <memory/object_allocator.h>

#include <utility/protobuf_mini_dumper.h>

#include <config/logic_config.h>
#include <data/player.h>

#include <rpc/rpc_async_invoke.h>

#include <unordered_set>
#include <utility>

#include "logic/async_jobs/task_action_player_remote_patch_jobs.h"

user_async_jobs_manager::user_async_jobs_manager(player& owner)
    : owner_(&owner), is_dirty_(false), remote_command_patch_task_next_timepoint_(0) {}

user_async_jobs_manager::~user_async_jobs_manager() {}

void user_async_jobs_manager::create_init(rpc::context&, uint32_t) {}

void user_async_jobs_manager::login_init(rpc::context&) { reset_async_jobs_protect(); }

void user_async_jobs_manager::refresh_feature_limit(rpc::context& ctx) { try_async_jobs(ctx); }

void user_async_jobs_manager::init_from_table_data(rpc::context&,
                                                   const PROJECT_NAMESPACE_ID::table_user& player_table) {
  if (player_table.has_async_job_blob_data()) {
    remote_command_patch_task_next_timepoint_ =
        static_cast<time_t>(player_table.async_job_blob_data().async_jobs().next_task_active_time());

    history_uuids_.clear();
    for (int i = 0; i < player_table.async_job_blob_data().async_jobs().history_size(); ++i) {
      auto& history_db_data = player_table.async_job_blob_data().async_jobs().history(i);
      history_item item;
      item.timeout = history_db_data.timeout();
      history_uuids_[history_db_data.job_type()].insert_key_value(history_db_data.action_uuid(), item);
    }

    retry_jobs_.clear();
    for (int i = 0; i < player_table.async_job_blob_data().retry_jobs_size(); ++i) {
      auto& retry_job = player_table.async_job_blob_data().retry_jobs(i);
      retry_jobs_[retry_job.job_type()][retry_job.job_data().action_uuid()] =
          atfw::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::user_async_jobs_blob_data>(
              retry_job.job_data());
    }

    std::unordered_set<int32_t> cleanup_queue;
    cleanup_queue.reserve(history_uuids_.size());
    for (auto& job_type : history_uuids_) {
      cleanup_queue.insert(job_type.first);
    }
    for (auto& job_type : cleanup_queue) {
      clear_job_uuids(job_type);
    }

    is_dirty_ = false;

    is_dirty_ = false;
  }
}

int user_async_jobs_manager::dump(rpc::context&, PROJECT_NAMESPACE_ID::table_user& user) const {
  auto async_jobs_data = user.mutable_async_job_blob_data();
  PROJECT_NAMESPACE_ID::player_async_jobs_data* jobs_data = async_jobs_data->mutable_async_jobs();
  if (NULL == jobs_data) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  time_t now = atfw::util::time::time_utility::get_now();

  jobs_data->set_next_task_active_time(static_cast<int64_t>(remote_command_patch_task_next_timepoint_));
  for (auto& job_type : const_cast<user_async_jobs_manager*>(this)->history_uuids_) {
    for (auto& job_data_pair : job_type.second) {
      if (!job_data_pair.second) {
        continue;
      }

      if (job_data_pair.second->timeout <= now) {
        continue;
      }

      auto history_db_data = jobs_data->add_history();
      if (nullptr == history_db_data) {
        continue;
      }
      history_db_data->set_job_type(job_type.first);
      history_db_data->set_action_uuid(job_data_pair.first);
      history_db_data->set_timeout(job_data_pair.second->timeout);
    }
  }

  for (auto& job_type : retry_jobs_) {
    for (auto& job_data_pair : job_type.second) {
      if (!job_data_pair.second) {
        continue;
      }

      auto retry_db_data = async_jobs_data->add_retry_jobs();
      if (nullptr == retry_db_data) {
        continue;
      }
      retry_db_data->set_job_type(job_type.first);
      protobuf_copy_message(*retry_db_data->mutable_job_data(), *job_data_pair.second);
    }
  }

  return 0;
}

bool user_async_jobs_manager::is_dirty() const { return is_dirty_; }

void user_async_jobs_manager::clear_dirty() { is_dirty_ = false; }

bool user_async_jobs_manager::is_async_jobs_task_running() const {
  if (task_type_trait::empty(remote_command_patch_task_)) {
    return false;
  }

  if (task_type_trait::is_exiting(remote_command_patch_task_)) {
    task_type_trait::reset_task(remote_command_patch_task_);
    return false;
  }

  return true;
}

bool user_async_jobs_manager::try_async_jobs(rpc::context& ctx) {
  if (!owner_->is_inited()) {
    return false;
  }

  // 保护时间
  if (::util::time::time_utility::get_now() <= remote_command_patch_task_next_timepoint_ &&
      force_async_job_type_.empty()) {
    return false;
  }

  // 只允许一个任务进行
  if (is_async_jobs_task_running()) {
    return false;
  }

  // 玩家临时性登出，暂时也不需要patch数据
  if (NULL == owner_ || !owner_->has_session()) {
    return false;
  }

  is_dirty_ = true;
  if (::util::time::time_utility::get_now() > remote_command_patch_task_next_timepoint_) {
    force_async_job_type_.clear();
    remote_command_patch_task_next_timepoint_ = atfw::util::time::time_utility::get_now() +
                                                logic_config::me()->get_server_cfg().user().async_job().interval().seconds();
  }

  task_type_trait::task_type task_inst;
  task_action_player_remote_patch_jobs::ctor_param_t params;
  params.user = owner_->shared_from_this();
  params.timeout_duration =
      task_manager::make_timeout_duration(logic_config::me()->get_server_cfg().user().async_job().timeout());
  params.timeout_timepoint = atfw::util::time::time_utility::now() + params.timeout_duration;
  params.caller_context = &ctx;
  params.async_job_type.swap(force_async_job_type_);
  task_manager::me()->create_task_with_timeout<task_action_player_remote_patch_jobs>(task_inst, params.timeout_duration,
                                                                                     std::move(params));

  if (task_type_trait::empty(task_inst)) {
    FWLOGERROR("create task_action_player_remote_patch_jobs failed");
  } else {
    remote_command_patch_task_ = task_inst;

    dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

    int res = task_manager::me()->start_task(task_inst, start_data);
    if (res < 0) {
      FWPLOGERROR(*owner_, "start task_action_player_remote_patch_jobs failed, res: {}({})", res,
                  protobuf_mini_dumper_get_error_msg(res));
      task_type_trait::reset_task(remote_command_patch_task_);
      return false;
    }
  }

  return true;
}

rpc::result_code_type user_async_jobs_manager::wait_for_async_task(rpc::context& ctx) {
  if (!is_async_jobs_task_running()) {
    RPC_RETURN_CODE(0);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, remote_command_patch_task_)));
}

void user_async_jobs_manager::force_async_job(int32_t jobs_type) {
  if (!owner_->is_inited()) {
    return;
  }

  force_async_job_type_.insert(jobs_type);
  is_dirty_ = true;
}

void user_async_jobs_manager::reset_async_jobs_protect() {
  if (0 != remote_command_patch_task_next_timepoint_) {
    remote_command_patch_task_next_timepoint_ = 0;
    is_dirty_ = true;
  }
}

void user_async_jobs_manager::clear_job_uuids(int32_t job_type) {
  auto iter = history_uuids_.find(job_type);
  if (iter == history_uuids_.end()) {
    return;
  }

  // uint32_t max_queue_size = logic_config::me()->get_server_cfg().user().async_job().retry_queue_size();
  // if (max_queue_size <= 0) {
  //   max_queue_size = 100;
  // }

  uint32_t conflict_checking_queue_size =
      logic_config::me()->get_server_cfg().user().async_job().conflict_checking_queue_size();
  if (conflict_checking_queue_size <= 0) {
    conflict_checking_queue_size = 1000;
  }

  time_t now = atfw::util::time::time_utility::get_now();
  while (!iter->second.empty()) {
    auto& top = iter->second.front();
    if (!top.second) {
      iter->second.pop_front();
      is_dirty_ = true;
      continue;
    }

    if (now <= top.second->timeout && iter->second.size() <= conflict_checking_queue_size) {
      break;
    }

    iter->second.pop_front();
    is_dirty_ = true;
  }
}

void user_async_jobs_manager::add_job_uuid(int32_t job_type, const std::string& uuid) {
  if (job_type <= 0) {
    return;
  }

  auto& history_set = history_uuids_[job_type];

  int64_t conflict_checking_timeout =
      logic_config::me()->get_server_cfg().user().async_job().conflict_checking_timeout().seconds();
  uint32_t conflict_checking_queue_size =
      logic_config::me()->get_server_cfg().user().async_job().conflict_checking_queue_size();
  if (conflict_checking_queue_size <= 0) {
    conflict_checking_queue_size = 1000;
  }

  if (conflict_checking_timeout <= 0) {
    conflict_checking_timeout = 1800;
  }

  time_t now = atfw::util::time::time_utility::get_now();
  history_item item;
  item.timeout = now + conflict_checking_timeout;

  history_set.insert_key_value(uuid, item);

  // 清理冗余队列
  while (!history_set.empty()) {
    auto& top = history_set.front();
    if (!top.second) {
      history_set.pop_front();
      continue;
    }

    if (now <= top.second->timeout && history_set.size() <= conflict_checking_queue_size) {
      break;
    }

    history_set.pop_front();
  }

  is_dirty_ = true;
}

bool user_async_jobs_manager::is_job_uuid_exists(int32_t job_type, const std::string& uuid) {
  auto iter_history = history_uuids_.find(job_type);
  if (iter_history != history_uuids_.end()) {
    if (iter_history->second.end() != iter_history->second.find(uuid, false)) {
      return true;
    }
  }

  auto iter_retry_job = retry_jobs_.find(job_type);
  if (iter_retry_job != retry_jobs_.end()) {
    if (iter_retry_job->second.end() != iter_retry_job->second.find(uuid)) {
      return true;
    }
  }

  return false;
}

void user_async_jobs_manager::add_retry_job(int32_t job_type, const async_job_ptr_type& job_data) {
  if (job_type <= 0 || !job_data) {
    return;
  }
  if (job_data->action_uuid().empty()) {
    return;
  }

  auto& job_set = retry_jobs_[job_type];

  auto& job_ptr = job_set[job_data->action_uuid()];
  if (!job_ptr) {
    job_ptr = job_data;
  } else {
    job_ptr->set_left_retry_times(job_ptr->left_retry_times() - 1);
  }
}

void user_async_jobs_manager::remove_retry_job(int32_t job_type, const std::string& uuid) {
  auto iter_retry_job = retry_jobs_.find(job_type);
  if (iter_retry_job == retry_jobs_.end()) {
    return;
  }

  iter_retry_job->second.erase(uuid);
}

std::vector<user_async_jobs_manager::async_job_ptr_type> user_async_jobs_manager::get_retry_jobs(
    int32_t job_type) const {
  std::vector<async_job_ptr_type> ret;

  auto iter_retry_job = retry_jobs_.find(job_type);
  if (iter_retry_job == retry_jobs_.end()) {
    return ret;
  }

  ret.reserve(iter_retry_job->second.size());
  for (auto& job_ptr : iter_retry_job->second) {
    if (job_ptr.second) {
      ret.push_back(job_ptr.second);
    }
  }

  return ret;
}
