// Copyright 2022 atframework
// Created by owent on 2022/02/15.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <libcopp/future/poller.h>

#include <stdint.h>
#include <cstddef>
#include <type_traits>

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

  bool is_success() const noexcept;
  bool is_error() const noexcept;

  inline bool is_ready() const noexcept { return result_data_.is_ready(); }

 private:
  copp::future::poller<value_type> result_data_;
};
}  // namespace rpc

// When using c++20 coroutine, declare RPC_AWAIT_CODE_RESULT like this
//   #define RPC_AWAIT_IGNORE_RESULT(x) ((void)(co_await x))
//   #define RPC_AWAIT_CODE_RESULT(x) (co_await x)
#define RPC_AWAIT_IGNORE_RESULT(x) ((void)(x))
#define RPC_AWAIT_CODE_RESULT(x) static_cast<int32_t>(x)
