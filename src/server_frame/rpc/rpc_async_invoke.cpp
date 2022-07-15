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
rpc_result<task_types::task_ptr_type, int> async_invoke(context &ctx, gsl::string_view name,
                                                        std::function<result_code_type(context &)> fn,
                                                        std::chrono::system_clock::duration timeout) {
  if (!fn) {
    return rpc_result<task_types::task_ptr_type, int>::make_error(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  task_types::task_ptr_type task_ptr;
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
    return rpc_result<task_types::task_ptr_type, int>::make_error(res);
  }

  task_manager::start_data_t start_data = dispatcher_make_default<dispatcher_start_data_t>();
  res = task_manager::me()->start_task(task_ptr->get_id(), start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_async_invoke {} with name rpc.async_invoke:{} failed, res: {}({})",
               task_ptr->get_id(), name, res, protobuf_mini_dumper_get_error_msg(res));
    return rpc_result<task_types::task_ptr_type, int>::make_error(res);
  }

  return rpc_result<task_types::task_ptr_type, int>::make_success(std::move(task_ptr));
}

rpc_result<task_types::task_ptr_type, int> async_invoke(gsl::string_view caller_name, gsl::string_view name,
                                                        std::function<result_code_type(context &)> fn,
                                                        std::chrono::system_clock::duration timeout) {
  rpc::context ctx;
  return async_invoke(ctx, name, std::move(fn), timeout);
}

result_code_type wait_tasks(const std::vector<task_types::task_ptr_type> &tasks) {
  task_types::task_ptr_type self_task(task_manager::task_t::this_task());
  if (!self_task) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }

  while (true) {
    if (self_task->is_timeout()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    } else if (self_task->is_faulted()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    } else if (self_task->is_canceled()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
    } else if (self_task->is_exiting()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING);
    }

    task_types::task_ptr_type last_task;
    for (auto &task : tasks) {
      if (!task || task == self_task) {
        continue;
      }

      if (!task) {
        continue;
      }

      if (task->is_exiting()) {
        continue;
      }

      last_task = task;
    }

    if (!last_task) {
      break;
    }

    self_task->await_task(last_task);
  }

  RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace rpc
