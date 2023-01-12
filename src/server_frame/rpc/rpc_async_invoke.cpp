// Copyright 2021 atframework
// Created by owent on 2021-11-01.
//

#include "rpc/rpc_async_invoke.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/task_manager.h>

#include <logic/action/task_action_async_invoke.h>

#include "rpc/rpc_utils.h"

namespace rpc {
async_invoke_result async_invoke(context &ctx, gsl::string_view name, std::function<result_code_type(context &)> fn,
                                 std::chrono::system_clock::duration timeout) {
  if (!fn) {
    return async_invoke_result::make_error(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  task_type_trait::task_type task_ptr;
  task_action_async_invoke::ctor_param_t params;
  params.caller_context = &ctx;
  params.callable = std::move(fn);
  params.name = util::log::format("rpc.async_invoke:{}", name);
  int res;
  if (timeout > std::chrono::system_clock::duration::zero()) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    res = task_manager::me()->create_task_with_timeout<task_action_async_invoke>(
        task_ptr, static_cast<time_t>(seconds.count()),
        static_cast<time_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - seconds).count()),
        std::move(params));
  } else {
    res = task_manager::me()->create_task<task_action_async_invoke>(task_ptr, std::move(params));
  }
  if (0 != res || !task_ptr) {
    FWLOGERROR("create task_action_async_invoke failed, res: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
    return async_invoke_result::make_error(res);
  }

  dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
  res = task_manager::me()->start_task(task_ptr->get_id(), start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_async_invoke {} with name rpc.async_invoke:{} failed, res: {}({})",
               task_ptr->get_id(), name, res, protobuf_mini_dumper_get_error_msg(res));
    return async_invoke_result::make_error(res);
  }

  return async_invoke_result::make_success(std::move(task_ptr));
}

async_invoke_result async_invoke(gsl::string_view caller_name, gsl::string_view name,
                                 std::function<result_code_type(context &)> fn,
                                 std::chrono::system_clock::duration timeout) {
  rpc::context ctx;
  return async_invoke(ctx, name, std::move(fn), timeout);
}

result_code_type wait_tasks(context &ctx, const std::vector<task_type_trait::task_type> &tasks) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{} should be called in a task", "rpc::wait_tasks");

  while (true) {
    if (TASK_COMPAT_CHECK_IS_TIMEOUT()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    } else if (TASK_COMPAT_CHECK_IS_FAULT()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    } else if (TASK_COMPAT_CHECK_IS_CANCEL()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
    } else if (TASK_COMPAT_CHECK_IS_EXITING()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING);
    }

    const task_type_trait::task_type *last_task = nullptr;
    for (auto &task : tasks) {
      if (task_type_trait::empty(task) || task_type_trait::get_task_id(task) == ctx.get_task_context().task_id) {
        continue;
      }

      if (task_type_trait::empty(task)) {
        continue;
      }

      if (task_type_trait::is_exiting(task)) {
        continue;
      }

      last_task = &task;
    }

    if (last_task == nullptr || task_type_trait::empty(*last_task)) {
      break;
    }

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    co_await *last_task;
#else
    task_type_trait::internal_task_type::this_task()->await_task(task_type_trait::task_type{*last_task});
#endif
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

result_code_type wait_task(context &ctx, const task_type_trait::task_type &other_task) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{} should be called in a task", "rpc::wait_task");

  while (true) {
    // other_task mey be changed after await_task is returned
    if (task_type_trait::empty(other_task)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    if (task_type_trait::get_task_id(other_task) == ctx.get_task_context().task_id) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    if (TASK_COMPAT_CHECK_IS_TIMEOUT()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    } else if (TASK_COMPAT_CHECK_IS_FAULT()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    } else if (TASK_COMPAT_CHECK_IS_CANCEL()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
    } else if (TASK_COMPAT_CHECK_IS_EXITING()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING);
    }

    if (task_type_trait::is_exiting(other_task)) {
      break;
    }

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    co_await other_task;
#else
    task_type_trait::internal_task_type::this_task()->await_task(other_task);
#endif
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace rpc
