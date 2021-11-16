// Copyright 2021 atframework
// Created by owent on 2021/11/01.
//

#include "rpc/rpc_async_invoke.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>

#include <utility/protobuf_mini_dumper.h>

#include <logic/action/task_action_async_invoke.h>

#include "rpc/rpc_utils.h"

namespace rpc {
rpc_result<task_manager::task_ptr_t, int> async_invoke(context &ctx, gsl::string_view name,
                                                       std::function<task_action_base::result_type(context &)> fn) {
  if (!fn) {
    return rpc_result<task_manager::task_ptr_t, int>::make_error(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  task_manager::task_ptr_t task_ptr;
  task_action_async_invoke::ctor_param_t params;
  params.caller_context = &ctx;
  params.callable = std::move(fn);
  params.name = util::log::format("rpc.async_invoke:{}", name);
  int res = task_manager::me()->create_task<task_action_async_invoke>(task_ptr, std::move(params));
  if (0 != res || !task_ptr) {
    FWLOGERROR("create task_action_async_invoke failed, res: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
    return rpc_result<task_manager::task_ptr_t, int>::make_error(res);
  }

  task_manager::start_data_t start_data = dispatcher_make_default<dispatcher_start_data_t>();
  res = task_manager::me()->start_task(task_ptr->get_id(), start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_async_invoke {} with name rpc.async_invoke:{} failed, res: {}({})",
               task_ptr->get_id(), name, res, protobuf_mini_dumper_get_error_msg(res));
    return rpc_result<task_manager::task_ptr_t, int>::make_error(res);
  }

  return rpc_result<task_manager::task_ptr_t, int>::make_success(std::move(task_ptr));
}

rpc_result<task_manager::task_ptr_t, int> async_invoke(gsl::string_view caller_name, gsl::string_view name,
                                                       std::function<task_action_base::result_type(context &)> fn) {
  ::rpc::context ctx;
  ::rpc::context::tracer tracer;
  ::rpc::context::trace_option trace_option;
  trace_option.dispatcher = nullptr;
  trace_option.is_remote = false;
  trace_option.kind = atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;

  ctx.setup_tracer(tracer, ::rpc::context::tracer::string_view{caller_name.data(), caller_name.size()},
                   std::move(trace_option));

  return async_invoke(ctx, name, std::move(fn));
}

rpc_result<int, int> wait_tasks(const std::vector<task_manager::task_ptr_t> &tasks) {
  task_manager::task_ptr_t self_task(task_manager::task_t::this_task());
  if (!self_task) {
    return rpc_result<int, int>::make_error(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK);
  }

  while (true) {
    if (self_task->is_timeout()) {
      return rpc_result<int, int>::make_error(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
    } else if (self_task->is_faulted()) {
      return rpc_result<int, int>::make_error(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED);
    } else if (self_task->is_canceled()) {
      return rpc_result<int, int>::make_error(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED);
    } else if (self_task->is_exiting()) {
      return rpc_result<int, int>::make_error(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING);
    }

    task_manager::task_ptr_t last_task;
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

  return rpc_result<int, int>::make_success(PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS);
}

}  // namespace rpc
