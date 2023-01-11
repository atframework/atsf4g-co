// Copyright 2021 atframework
// Created by owent on 2018-05-01.
//

#include "logic/action/task_action_player_async_jobs.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <time/time_utility.h>

#include <data/player.h>

#include <logic/async_jobs/user_async_jobs_manager.h>

#include <rpc/rpc_async_invoke.h>

#include <assert.h>

task_action_player_async_jobs::task_action_player_async_jobs(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param) {}

task_action_player_async_jobs::~task_action_player_async_jobs() {}

task_action_player_async_jobs::result_type task_action_player_async_jobs::operator()() {
  if (!param_.user) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  set_user_key(param_.user->get_user_id(), param_.user->get_zone_id());

  // 这后面的都是玩家异步处理任务，一般用户刷新缓存和数据修复和数据patch。
  // 不成功不应该影响逻辑和数据，而是仅影响某些不重要的缓存滞后。

  if (param_.after && !param_.after->is_exiting()) {
    int ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(get_shared_context(), param_.after));
    if (ret < 0) {
      TASK_ACTION_RETURN_CODE(ret);
    }
  }
  // 这里必须reset，否则会循环引用然后泄漏
  param_.after.reset();

  // 某些数据拉取需要排队 一下等平台数据更新完
  // 启动玩家数据异步命令patch任务
  if (param_.user) {
    player::task_queue_lock_guard lock_guard(*param_.user);
    param_.user->get_user_async_jobs_manager().try_async_jobs(get_shared_context());
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_player_async_jobs::on_success() {
  if (param_.user) {
    FWPLOGDEBUG(*param_.user, "do {} success", "task_action_player_async_jobs");

    // 加入快速保存队列，确保玩家登入成功后保存一次在线状态
    param_.user->set_quick_save();
  }

  param_.user.reset();
  param_.after.reset();
  return get_result();
}

int task_action_player_async_jobs::on_failed() {
  if (param_.user) {
    FWPLOGERROR(*param_.user, "do task_action_player_async_jobs failed, res: {}", get_result());
  }

  param_.user.reset();
  param_.after.reset();
  return get_result();
}
