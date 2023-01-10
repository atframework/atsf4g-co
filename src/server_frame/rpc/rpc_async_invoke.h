// Copyright 2021 atframework
// Created by owent on 2021-11-01.
//

#pragma once

#include <gsl/select-gsl.h>
#include <std/explicit_declare.h>

#include <dispatcher/task_action_base.h>
#include <dispatcher/task_type_traits.h>

#include <stdint.h>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "rpc/rpc_common_types.h"

namespace rpc {
class context;

using async_invoke_result = util::design_pattern::result_type<task_type_trait::task_type, int>;

template <class... TARGS>
inline async_invoke_result make_async_invoke_success(TARGS &&...args) {
  return async_invoke_result::make_success(std::forward<TARGS>(args)...);
}

template <class... TARGS>
inline async_invoke_result make_async_invoke_error(TARGS &&...args) {
  return async_invoke_result::make_error(std::forward<TARGS>(args)...);
}

EXPLICIT_NODISCARD_ATTR async_invoke_result
async_invoke(context &ctx, gsl::string_view name, std::function<result_code_type(context &)> fn,
             std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero());

template <class TREP, class TPERIOD>
EXPLICIT_NODISCARD_ATTR inline async_invoke_result async_invoke(
    context &ctx, gsl::string_view name, std::function<result_code_type(context &)> fn,
    std::chrono::duration<TREP, TPERIOD> timeout = std::chrono::duration<TREP, TPERIOD>::zero()) {
  return async_invoke(ctx, name, std::move(fn),
                      std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout));
}

EXPLICIT_NODISCARD_ATTR async_invoke_result
async_invoke(gsl::string_view caller_name, gsl::string_view name, std::function<result_code_type(context &)> fn,
             std::chrono::system_clock::duration timeout = std::chrono::system_clock::duration::zero());

template <class TREP, class TPERIOD>
EXPLICIT_NODISCARD_ATTR inline async_invoke_result async_invoke(
    gsl::string_view caller_name, gsl::string_view name, std::function<result_code_type(context &)> fn,
    std::chrono::duration<TREP, TPERIOD> timeout = std::chrono::duration<TREP, TPERIOD>::zero()) {
  return async_invoke(caller_name, name, std::move(fn),
                      std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout));
}

EXPLICIT_NODISCARD_ATTR result_code_type wait_tasks(const std::vector<task_type_trait::task_type> &tasks);

EXPLICIT_NODISCARD_ATTR result_code_type wait_task(const task_type_trait::task_type &other_task);

}  // namespace rpc
