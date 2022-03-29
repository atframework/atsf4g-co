// Copyright 2021 atframework
// Created by owent on 2021-11-01.
//

#pragma once

#include <gsl/select-gsl.h>
#include <std/explicit_declare.h>

#include <dispatcher/task_action_base.h>
#include <dispatcher/task_manager.h>

#include <stdint.h>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "rpc/rpc_common_types.h"

namespace rpc {
class context;

template <class... TARGS>
rpc_result<task_manager::task_ptr_t, int> make_async_invoke_success(TARGS &&...args) {
  return rpc_result<task_manager::task_ptr_t, int>::make_success(std::forward<TARGS>(args)...);
}

template <class... TARGS>
rpc_result<task_manager::task_ptr_t, int> make_async_invoke_error(TARGS &&...args) {
  return rpc_result<task_manager::task_ptr_t, int>::make_error(std::forward<TARGS>(args)...);
}

EXPLICIT_NODISCARD_ATTR rpc_result<task_manager::task_ptr_t, int> async_invoke(
    context &ctx, gsl::string_view name, std::function<task_action_base::result_type(context &)> fn);

EXPLICIT_NODISCARD_ATTR rpc_result<task_manager::task_ptr_t, int> async_invoke(
    gsl::string_view caller_name, gsl::string_view name, std::function<task_action_base::result_type(context &)> fn);

EXPLICIT_NODISCARD_ATTR result_code_type wait_tasks(const std::vector<task_manager::task_ptr_t> &tasks);

}  // namespace rpc
