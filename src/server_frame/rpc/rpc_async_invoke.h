// Copyright 2021 atframework
// Created by owent on 2021/11/01.
//

#pragma once

#include <design_pattern/result_type.h>
#include <gsl/select-gsl.h>
#include <std/explicit_declare.h>

#include <dispatcher/task_action_base.h>
#include <dispatcher/task_manager.h>

#include <stdint.h>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace rpc {
class context;

template <class TSuccess, class TError>
class rpc_result : public util::design_pattern::result_type<TSuccess, TError> {
 public:
  using self_type = rpc_result<TSuccess, TError>;
  using base_type = util::design_pattern::result_type<TSuccess, TError>;

 public:
  explicit rpc_result(base_type &&origin_input) : base_type(std::move(origin_input)) {}

  template <class... TARGS>
  static inline self_type make_success(TARGS &&...args) {
    return rpc_result(base_type::make_success(std::forward<TARGS>(args)...));
  }

  template <class... TARGS>
  static inline self_type make_error(TARGS &&...args) {
    return rpc_result(base_type::make_error(std::forward<TARGS>(args)...));
  }
};

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

EXPLICIT_NODISCARD_ATTR rpc_result<int, int> wait_tasks(const std::vector<task_manager::task_ptr_t> &tasks);

}  // namespace rpc
