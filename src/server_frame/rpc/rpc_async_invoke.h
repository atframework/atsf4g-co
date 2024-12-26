// Copyright 2021 atframework
// Created by owent on 2021-11-01.
//

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>
#include <std/explicit_declare.h>

#include <log/log_wrapper.h>

#include <dispatcher/dispatcher_type_defines.h>
#include <dispatcher/task_type_traits.h>

#include <stdint.h>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "rpc/rpc_common_types.h"

namespace rpc {
class context;

using async_invoke_result = atfw::util::design_pattern::result_type<task_type_trait::task_type, int>;

template <class... TARGS>
UTIL_SYMBOL_VISIBLE inline async_invoke_result make_async_invoke_success(TARGS &&...args) {
  return async_invoke_result::make_success(std::forward<TARGS>(args)...);
}

template <class... TARGS>
UTIL_SYMBOL_VISIBLE inline async_invoke_result make_async_invoke_error(TARGS &&...args) {
  return async_invoke_result::make_error(std::forward<TARGS>(args)...);
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API async_invoke_result
async_invoke(context &ctx, gsl::string_view name, std::function<result_code_type(context &)> fn,
             std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero());

template <class TREP, class TPERIOD>
EXPLICIT_NODISCARD_ATTR UTIL_SYMBOL_VISIBLE inline async_invoke_result async_invoke(
    context &ctx, gsl::string_view name, std::function<result_code_type(context &)> fn,
    std::chrono::duration<TREP, TPERIOD> timeout = std::chrono::duration<TREP, TPERIOD>::zero()) {
  return async_invoke(ctx, name, std::move(fn),
                      std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout));
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API async_invoke_result
async_invoke(gsl::string_view caller_name, gsl::string_view name, std::function<result_code_type(context &)> fn,
             std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero());

template <class TREP, class TPERIOD>
EXPLICIT_NODISCARD_ATTR UTIL_SYMBOL_VISIBLE inline async_invoke_result async_invoke(
    gsl::string_view caller_name, gsl::string_view name, std::function<result_code_type(context &)> fn,
    std::chrono::duration<TREP, TPERIOD> timeout = std::chrono::duration<TREP, TPERIOD>::zero()) {
  return async_invoke(caller_name, name, std::move(fn),
                      std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout));
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_code_type wait_tasks(context &ctx,
                                                                     gsl::span<const task_type_trait::task_type> tasks);

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_code_type wait_tasks(context &ctx,
                                                                     gsl::span<task_type_trait::task_type> tasks);

template <class ContainerType>
EXPLICIT_NODISCARD_ATTR UTIL_SYMBOL_VISIBLE inline result_code_type wait_tasks(context &ctx, ContainerType &&tasks) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(wait_tasks(ctx, gsl::make_span(std::forward<ContainerType>(tasks)))));
}

EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_code_type wait_task(context &ctx,
                                                                    const task_type_trait::task_type &other_task);

SERVER_FRAME_API void async_then_start_task(context &ctx, gsl::string_view name, task_type_trait::task_type waiting,
                                            task_type_trait::id_type task_id);
SERVER_FRAME_API void async_then_start_task(context &ctx, gsl::string_view name, task_type_trait::task_type waiting,
                                            task_type_trait::task_type then_task);

template <class TCALLABLE, class... TARGS>
UTIL_SYMBOL_VISIBLE void async_then(context &ctx, gsl::string_view name, task_type_trait::task_type waiting,
                                    TCALLABLE &&callable, TARGS &&...args) {
  if (task_type_trait::empty(waiting) || task_type_trait::is_exiting(waiting)) {
    callable(std::forward<TARGS>(args)...);
    return;
  }

  async_invoke_result result = async_invoke(
      ctx, name, [waiting = std::move(waiting), callable, args...](rpc::context &child_ctx) -> rpc::result_code_type {
        auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(child_ctx, waiting));
        callable(std::forward<TARGS>(args)...);
        RPC_RETURN_CODE(ret);
      });

  if (result.is_success()) {
    return;
  }

  FWLOGERROR("Try to invoke task({}) to wait task {} and then call callable failed, try to call it directly.", name,
             task_type_trait::get_task_id(waiting));
  callable(std::forward<TARGS>(args)...);
}

template <class TCALLABLE, class... TARGS>
UTIL_SYMBOL_VISIBLE void async_then_with_context(context &ctx, gsl::string_view name,
                                                 task_type_trait::task_type waiting, TCALLABLE &&callable,
                                                 TARGS &&...args) {
  if (task_type_trait::empty(waiting) || task_type_trait::is_exiting(waiting)) {
    callable(ctx, std::forward<TARGS>(args)...);
    return;
  }

  async_invoke_result result = async_invoke(
      ctx, name, [waiting = std::move(waiting), callable, args...](rpc::context &child_ctx) -> rpc::result_code_type {
        auto ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(child_ctx, waiting));
        callable(child_ctx, std::forward<TARGS>(args)...);
        RPC_RETURN_CODE(ret);
      });

  if (result.is_success()) {
    return;
  }

  FWLOGERROR("Try to invoke task({}) to wait task {} and then call callable failed, try to call it directly.", name,
             task_type_trait::get_task_id(waiting));
  callable(ctx, std::forward<TARGS>(args)...);
}

}  // namespace rpc
