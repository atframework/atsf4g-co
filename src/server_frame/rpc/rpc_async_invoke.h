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

#include <chrono>
#include <functional>
#include <type_traits>
#include <utility>

#include "rpc/rpc_common_types.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
#  include <rpc/unit_test/mock_engine_bridge.h>
#endif

namespace rpc {
class context;

using async_invoke_result = atfw::util::design_pattern::result_type<task_type_trait::task_type, int>;

template <class... TARGS>
ATFW_UTIL_SYMBOL_VISIBLE inline async_invoke_result make_async_invoke_success(TARGS &&...args) {
  return async_invoke_result::make_success(std::forward<TARGS>(args)...);
}

template <class... TARGS>
ATFW_UTIL_SYMBOL_VISIBLE inline async_invoke_result make_async_invoke_error(TARGS &&...args) {
  return async_invoke_result::make_error(std::forward<TARGS>(args)...);
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API async_invoke_result
async_invoke(context &ctx, gsl::string_view name, std::function<result_code_type(context &)> fn,
             std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero());

template <class TREP, class TPERIOD>
ATFW_EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE inline async_invoke_result async_invoke(
    context &ctx, gsl::string_view name, std::function<result_code_type(context &)> fn,
    std::chrono::duration<TREP, TPERIOD> timeout = std::chrono::duration<TREP, TPERIOD>::zero()) {
  return async_invoke(ctx, name, std::move(fn),
                      std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout));
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API async_invoke_result
async_invoke(gsl::string_view caller_name, gsl::string_view name, std::function<result_code_type(context &)> fn,
             std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero());

template <class TREP, class TPERIOD>
ATFW_EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE inline async_invoke_result async_invoke(
    gsl::string_view caller_name, gsl::string_view name, std::function<result_code_type(context &)> fn,
    std::chrono::duration<TREP, TPERIOD> timeout = std::chrono::duration<TREP, TPERIOD>::zero()) {
  return async_invoke(caller_name, name, std::move(fn),
                      std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout));
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
namespace unit_test {
// §3.1 test seam：async_invoke mock。命名约定与 task_manager::mock_create_task 一致：一个词干
// mock_async_invoke 派生注册/清空/内部询问，回调类型以 hook 为唯一名词 async_invoke_hook_t。
// 回调按任务名判定：返回 0 或正数 = 不拦截；返回负数 = 以该错误码使 async_invoke 失败。
// 回调可通过 timeout 引用改写本次调用的超时（不拦截时修改依然生效）。返回 RAII handle，
// 析构自动卸载；可同时注册多个。生产构建完全裁剪。与 rpc mock bridge 相同约定：非线程安全，
// 注册与询问必须同线程（单测 pump 线程）。
using async_invoke_hook_t =
    std::function<int(gsl::string_view name, std::chrono::system_clock::duration &timeout)>;

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API mock_rule_handle mock_async_invoke(async_invoke_hook_t hook);

// 卸载全部已注册的 mock（runtime 重建/teardown 时兜底调用，防止用例泄漏）
SERVER_FRAME_API void mock_async_invoke_clear();
}  // namespace unit_test
#endif

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_code_type
wait_tasks(context &ctx, gsl::span<const task_type_trait::task_type> tasks);

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_code_type wait_tasks(context &ctx,
                                                                          gsl::span<task_type_trait::task_type> tasks);

template <class ContainerType>
ATFW_EXPLICIT_NODISCARD_ATTR ATFW_UTIL_SYMBOL_VISIBLE inline result_code_type wait_tasks(context &ctx,
                                                                                         ContainerType &&tasks) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(wait_tasks(ctx, gsl::make_span(std::forward<ContainerType>(tasks)))));
}

ATFW_EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API result_code_type wait_task(context &ctx,
                                                                         const task_type_trait::task_type &other_task);

SERVER_FRAME_API void async_then_start_task(context &ctx, gsl::string_view name, task_type_trait::task_type waiting,
                                            task_type_trait::id_type task_id);
SERVER_FRAME_API void async_then_start_task(context &ctx, gsl::string_view name, task_type_trait::task_type waiting,
                                            task_type_trait::task_type then_task);

template <class TCALLABLE>
ATFW_UTIL_SYMBOL_VISIBLE auto __async_then_copy_or_move_callable(TCALLABLE &&callable) {
  using return_type = typename std::remove_reference<TCALLABLE>::type;
  return return_type(std::forward<TCALLABLE>(callable));
}

template <class TCALLABLE, class... TARGS>
ATFW_UTIL_SYMBOL_VISIBLE void async_then(context &ctx, gsl::string_view name, task_type_trait::task_type waiting,
                                         TCALLABLE &&callable, TARGS &&...args) {
  if (task_type_trait::empty(waiting) || task_type_trait::is_exiting(waiting)) {
    callable(std::forward<TARGS>(args)...);
    return;
  }

  async_invoke_result result = async_invoke(
      ctx, name,
      // callable 必须复制或移动进lambda，以防wait_task后生命周期无效
      [waiting = std::move(waiting), callable = __async_then_copy_or_move_callable(std::forward<TCALLABLE>(callable)),
       args...](rpc::context &child_ctx) -> rpc::result_code_type {
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
ATFW_UTIL_SYMBOL_VISIBLE void async_then_with_context(context &ctx, gsl::string_view name,
                                                      task_type_trait::task_type waiting, TCALLABLE &&callable,
                                                      TARGS &&...args) {
  if (task_type_trait::empty(waiting) || task_type_trait::is_exiting(waiting)) {
    callable(ctx, std::forward<TARGS>(args)...);
    return;
  }

  async_invoke_result result = async_invoke(
      ctx, name,
      // callable 必须复制或移动进lambda，以防wait_task后生命周期无效
      [waiting = std::move(waiting), callable = __async_then_copy_or_move_callable(std::forward<TCALLABLE>(callable)),
       args...](rpc::context &child_ctx) -> rpc::result_code_type {
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
