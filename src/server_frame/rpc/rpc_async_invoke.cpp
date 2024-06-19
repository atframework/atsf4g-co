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
SERVER_FRAME_API async_invoke_result async_invoke(context &ctx, gsl::string_view name,
                                                  std::function<result_code_type(context &)> fn,
                                                  std::chrono::system_clock::duration timeout) {
  if (!fn) {
    return async_invoke_result::make_error(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  task_type_trait::task_type task_inst;
  task_action_async_invoke::ctor_param_t params;
  params.caller_context = &ctx;
  params.callable = std::move(fn);
  params.name = util::log::format("rpc.async_invoke:{}", name);
  int res;
  if (timeout > std::chrono::system_clock::duration::zero()) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    res = task_manager::me()->create_task_with_timeout<task_action_async_invoke>(
        task_inst, static_cast<time_t>(seconds.count()),
        static_cast<time_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - seconds).count()),
        std::move(params));
  } else {
    res = task_manager::me()->create_task<task_action_async_invoke>(task_inst, std::move(params));
  }
  if (0 != res || task_type_trait::empty(task_inst)) {
    FWLOGERROR("create task_action_async_invoke failed, res: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
    return async_invoke_result::make_error(res);
  }

  dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
  res = task_manager::me()->start_task(task_type_trait::get_task_id(task_inst), start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_async_invoke {} with name rpc.async_invoke:{} failed, res: {}({})",
               task_type_trait::get_task_id(task_inst), name, res, protobuf_mini_dumper_get_error_msg(res));
    return async_invoke_result::make_error(res);
  }

  return async_invoke_result::make_success(std::move(task_inst));
}

SERVER_FRAME_API async_invoke_result async_invoke(gsl::string_view, gsl::string_view name,
                                                  std::function<result_code_type(context &)> fn,
                                                  std::chrono::system_clock::duration timeout) {
  rpc::context ctx{rpc::context::create_without_task()};
  return async_invoke(ctx, name, std::move(fn), timeout);
}

SERVER_FRAME_API result_code_type wait_tasks(context &ctx, gsl::span<const task_type_trait::task_type> tasks) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{} should be called in a task", "rpc::wait_tasks");

  while (true) {
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_timeout(current_task_status)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    } else if (task_type_trait::is_fault(current_task_status)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    } else if (task_type_trait::is_cancel(current_task_status)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
    } else if (task_type_trait::is_exiting(current_task_status)) {
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
    task_type_trait::task_type copy_task{*last_task};
    co_await copy_task;
#else
    task_type_trait::internal_task_type::this_task()->await_task(task_type_trait::task_type{*last_task});
#endif
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API result_code_type wait_tasks(context &ctx, gsl::span<task_type_trait::task_type> tasks) {
  RPC_RETURN_CODE(
      RPC_AWAIT_CODE_RESULT(wait_tasks(ctx, gsl::span<const task_type_trait::task_type>{tasks.data(), tasks.size()})));
}

SERVER_FRAME_API result_code_type wait_task(context &ctx, const task_type_trait::task_type &other_task) {
  TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{} should be called in a task", "rpc::wait_task");

  while (true) {
    // other_task mey be changed after await_task is returned
    if (task_type_trait::empty(other_task)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    if (task_type_trait::get_task_id(other_task) == ctx.get_task_context().task_id) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_timeout(current_task_status)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    } else if (task_type_trait::is_fault(current_task_status)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    } else if (task_type_trait::is_cancel(current_task_status)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
    } else if (task_type_trait::is_exiting(current_task_status)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING);
    }

    if (task_type_trait::is_exiting(other_task)) {
      break;
    }

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    task_type_trait::task_type copy_task{other_task};
    co_await copy_task;
#else
    task_type_trait::internal_task_type::this_task()->await_task(other_task);
#endif
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

SERVER_FRAME_API void async_then_start_task(context &ctx, gsl::string_view name, task_type_trait::task_type waiting,
                                            task_type_trait::id_type task_id) {
  if (task_type_trait::empty(waiting) || task_type_trait::is_exiting(waiting)) {
    if (task_manager::is_instance_destroyed()) {
      return;
    }
    dispatcher_start_data_type data = dispatcher_make_default<dispatcher_start_data_type>();
    data.context = &ctx;
    task_manager::me()->start_task(task_id, data);
    return;
  }

  task_type_trait::id_type waiting_task_id = task_type_trait::get_task_id(waiting);

  async_invoke_result result = async_invoke(
      ctx, name, [waiting = std::move(waiting), task_id](rpc::context &child_ctx) -> rpc::result_code_type {
        auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(child_ctx, waiting));
        if (task_manager::is_instance_destroyed()) {
          RPC_RETURN_CODE(ret);
        }

        dispatcher_start_data_type data = dispatcher_make_default<dispatcher_start_data_type>();
        data.context = &child_ctx;
        task_manager::me()->start_task(task_id, data);
        RPC_RETURN_CODE(ret);
      });

  if (result.is_success()) {
    return;
  }

  FWLOGERROR("Try to invoke task({}) to wait task {} and then start task {} failed, try to start task directly.", name,
             waiting_task_id, task_id);

  if (task_manager::is_instance_destroyed()) {
    return;
  }
  dispatcher_start_data_type data = dispatcher_make_default<dispatcher_start_data_type>();
  data.context = &ctx;
  task_manager::me()->start_task(task_id, data);
}

}  // namespace rpc
