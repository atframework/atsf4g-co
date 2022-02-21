// Copyright 2022 atframework
// Created by owent on 2022/02/15.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <design_pattern/result_type.h>

#include <libcopp/future/poller.h>

#include <stdint.h>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "rpc/rpc_macros.h"

namespace rpc {
class result_code_type {
 public:
  using value_type = int32_t;

 public:
  result_code_type();
  explicit result_code_type(value_type code);

  // Remove this and implement co_yield to get the result in the future
  explicit operator value_type() const noexcept;

  inline bool is_ready() const noexcept { return result_data_.is_ready(); }

 private:
  copp::future::poller<value_type> result_data_;
};

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

}  // namespace rpc

// When using c++20 coroutine, declare RPC_AWAIT_CODE_RESULT like this
//   #define RPC_AWAIT_IGNORE_RESULT(x) ((void)(co_await x))
//   #define RPC_AWAIT_CODE_RESULT(x) (co_await x)
#define RPC_AWAIT_IGNORE_RESULT(x) ((void)(x))
#define RPC_AWAIT_CODE_RESULT(x) static_cast<int32_t>(x)
