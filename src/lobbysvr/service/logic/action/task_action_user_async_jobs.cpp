// Copyright 2021 atframework
// Created by owent on 2018-05-01.
//

#include "logic/action/task_action_user_async_jobs.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <time/time_utility.h>

#include <data/user.h>

#include <logic/async_jobs/user_async_jobs_manager.h>

#include <rpc/rpc_async_invoke.h>

#include <assert.h>

task_action_user_async_jobs::task_action_user_async_jobs(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param) {}

task_action_user_async_jobs::~task_action_user_async_jobs() {}

task_action_user_async_jobs::result_type task_action_user_async_jobs::operator()() {
  if (!param_.user_inst) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  set_user_key(param_.user_inst->get_user_id(), param_.user_inst->get_zone_id());

  // 这后面的都是用户异步处理任务，一般用户刷新缓存和数据修复和数据patch。
  // 不成功不应该影响逻辑和数据，而是仅影响某些不重要的缓存滞后。

  if (!task_type_trait::empty(param_.after) && !task_type_trait::is_exiting(param_.after)) {
    int ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(get_shared_context(), param_.after));
    if (ret < 0) {
      TASK_ACTION_RETURN_CODE(ret);
    }
  }
  // 这里必须reset，否则会循环引用然后泄漏
  task_type_trait::reset_task(param_.after);

  // 某些数据拉取需要排队 一下等平台数据更新完
  // 启动用户数据异步命令patch任务
  if (param_.user_inst) {
    param_.user_inst->get_user_async_jobs_manager().try_async_jobs(get_shared_context());
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_user_async_jobs::on_success() {
  if (param_.user_inst) {
    FWLOGDEBUG("{} do {} success", *param_.user_inst, "task_action_user_async_jobs");

    // 加入快速保存队列，确保用户登入成功后保存一次在线状态
    param_.user_inst->set_quick_save();
  }

  param_.user_inst.reset();
  param_.after.reset();
  return get_result();
}

int task_action_user_async_jobs::on_failed() {
  if (param_.user_inst) {
    FWLOGERROR("{} do task_action_user_async_jobs failed, res: {}", *param_.user_inst, get_result());
  }

  param_.user_inst.reset();
  param_.after.reset();
  return get_result();
}
