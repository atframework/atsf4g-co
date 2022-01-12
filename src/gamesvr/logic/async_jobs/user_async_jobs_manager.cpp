// Copyright 2021 atframework
// Created by owent

#include "logic/async_jobs/user_async_jobs_manager.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <utility/protobuf_mini_dumper.h>

#include <config/logic_config.h>
#include <data/player.h>

#include <utility>

#include "logic/async_jobs/task_action_player_remote_patch_jobs.h"

user_async_jobs_manager::user_async_jobs_manager(player& owner)
    : owner_(&owner), is_dirty_(false), remote_command_patch_task_next_timepoint_(0) {}

user_async_jobs_manager::~user_async_jobs_manager() {}

void user_async_jobs_manager::create_init(rpc::context&, uint32_t) {
  // do nothing
}

void user_async_jobs_manager::login_init(rpc::context&) { reset_async_jobs_protect(); }

void user_async_jobs_manager::refresh_feature_limit(rpc::context& ctx) { try_async_jobs(ctx); }

void user_async_jobs_manager::init_from_table_data(rpc::context&,
                                                   const PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user& player_table) {
  if (player_table.has_async_jobs()) {
    remote_command_patch_task_next_timepoint_ = static_cast<time_t>(player_table.async_jobs().next_task_active_time());

    lastest_uuids_.clear();
    for (int i = 0; i < player_table.async_jobs().lasted_action_uuids_size(); ++i) {
      lastest_uuids_.insert(player_table.async_jobs().lasted_action_uuids(i));
    }

    is_dirty_ = false;
  }
}

int user_async_jobs_manager::dump(rpc::context&, PROJECT_SERVER_FRAME_NAMESPACE_ID::table_user& user) const {
  PROJECT_SERVER_FRAME_NAMESPACE_ID::player_async_jobs_data* jobs_data = user.mutable_async_jobs();
  if (NULL == jobs_data) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  jobs_data->set_next_task_active_time(static_cast<int64_t>(remote_command_patch_task_next_timepoint_));
  for (const std::string& uuid : lastest_uuids_) {
    jobs_data->add_lasted_action_uuids(uuid);
  }

  return 0;
}

bool user_async_jobs_manager::is_dirty() const { return is_dirty_; }

void user_async_jobs_manager::clear_dirty() { is_dirty_ = false; }

bool user_async_jobs_manager::is_async_jobs_task_running() const {
  if (!remote_command_patch_task_) {
    return false;
  }

  if (remote_command_patch_task_->is_exiting()) {
    remote_command_patch_task_.reset();
    return false;
  }

  return true;
}

bool user_async_jobs_manager::try_async_jobs(rpc::context& ctx) {
  // 保护时间
  if (::util::time::time_utility::get_now() <= remote_command_patch_task_next_timepoint_) {
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
  remote_command_patch_task_next_timepoint_ =
      ::util::time::time_utility::get_now() + logic_config::me()->get_logic().user().async_job().interval().seconds();

  task_manager::id_t tid = 0;
  task_action_player_remote_patch_jobs::ctor_param_t params;
  params.user = owner_->shared_from_this();
  params.timeout_duration = logic_config::me()->get_logic().user().async_job().timeout().seconds();
  params.timeout_timepoint = util::time::time_utility::get_now() + params.timeout_duration;
  params.caller_context = &ctx;
  task_manager::me()->create_task_with_timeout<task_action_player_remote_patch_jobs>(tid, params.timeout_duration,
                                                                                     COPP_MACRO_STD_MOVE(params));

  if (0 == tid) {
    FWLOGERROR("create task_action_player_remote_patch_jobs failed");
  } else {
    remote_command_patch_task_ = task_manager::me()->get_task(tid);

    dispatcher_start_data_t start_data = dispatcher_make_default<dispatcher_start_data_t>();

    int res = task_manager::me()->start_task(tid, start_data);
    if (res < 0) {
      FWPLOGERROR(*owner_, "start task_action_player_remote_patch_jobs failed, res: {}({})", res,
                  protobuf_mini_dumper_get_error_msg(res));
      remote_command_patch_task_.reset();
      return false;
    }
  }

  return true;
}

int user_async_jobs_manager::wait_for_async_task() {
  if (!is_async_jobs_task_running()) {
    return 0;
  }

  task_manager::task_t* current = task_manager::task_t::this_task();
  if (NULL == current) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK;
  }

  int res = current->await_task(remote_command_patch_task_);
  if (current->is_timeout()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  }

  if (current->is_faulted()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
  }

  if (current->is_canceled()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
  }

  if (current->is_exiting()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING;
  }

  return res;
}

void user_async_jobs_manager::reset_async_jobs_protect() {
  if (0 != remote_command_patch_task_next_timepoint_) {
    remote_command_patch_task_next_timepoint_ = 0;
    is_dirty_ = true;
  }
}

void user_async_jobs_manager::clear_job_uuids() {
  if (!lastest_uuids_.empty()) {
    is_dirty_ = true;
  }
  lastest_uuids_.clear();
}

void user_async_jobs_manager::add_job_uuid(const std::string& uuid) {
  if (lastest_uuids_.insert(uuid).second) {
    is_dirty_ = true;
  }
}

bool user_async_jobs_manager::is_job_uuid_exists(const std::string& uuid) const {
  return lastest_uuids_.end() != lastest_uuids_.find(uuid);
}
