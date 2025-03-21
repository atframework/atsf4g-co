// Copyright 2021 atframework
// Created by owent on 2018-04-06.
//

#pragma once

#include <config/compile_optimize.h>
#include <config/compiler_features.h>
#include <std/explicit_declare.h>

#include <stdint.h>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <functional>

namespace atframework {
class DispatcherOptions;
class RpcTraceSpan;
}  // namespace atframework

namespace rpc {
class context;
}

template <class T>
T dispatcher_make_default() noexcept;

struct ATFW_UTIL_SYMBOL_VISIBLE dispatcher_raw_message {
  uintptr_t message_type;  // 建议对所有的消息体类型分配一个ID，用以检查回调类型转换。推荐时使用dispatcher单例的地址。
  void *msg_addr;

  ATFW_UTIL_FORCEINLINE dispatcher_raw_message(const dispatcher_raw_message &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_raw_message(dispatcher_raw_message &&) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_raw_message &operator=(const dispatcher_raw_message &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_raw_message &operator=(dispatcher_raw_message &&) = default;

 private:
  ATFW_UTIL_FORCEINLINE dispatcher_raw_message() noexcept : message_type(0), msg_addr(nullptr) {}

  template <class T>
  ATFW_UTIL_SYMBOL_VISIBLE friend T dispatcher_make_default() noexcept;
};

template <>
ATFW_UTIL_FORCEINLINE dispatcher_raw_message dispatcher_make_default<dispatcher_raw_message>() noexcept {
  return dispatcher_raw_message();
}

struct ATFW_UTIL_SYMBOL_VISIBLE dispatcher_resume_data_type {
  dispatcher_raw_message message;  // 异步回调中用于透传消息体
  void *private_data;              // 异步回调中用于透传额外的私有数据
  uint64_t sequence;               // 等待序号，通常和发送序号匹配。用于检测同类消息是否是发出的那个

  rpc::context *context;  // 上下文对象，用于优化pb消息的生命周期和链路跟踪

  ATFW_UTIL_FORCEINLINE dispatcher_resume_data_type(const dispatcher_resume_data_type &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_resume_data_type(dispatcher_resume_data_type &&) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_resume_data_type &operator=(const dispatcher_resume_data_type &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_resume_data_type &operator=(dispatcher_resume_data_type &&) = default;

 private:
  ATFW_UTIL_FORCEINLINE dispatcher_resume_data_type() noexcept
      : message(dispatcher_make_default<dispatcher_raw_message>()),
        private_data(nullptr),
        sequence(0),
        context(nullptr) {}

  template <class T>
  friend T dispatcher_make_default() noexcept;
};

struct dispatcher_start_data_type {
  dispatcher_raw_message message;                 // 启动回调中用于透传消息体
  void *private_data;                             // 启动回调中用于透传额外的私有数据
  const atframework::DispatcherOptions *options;  // 调度协议层选项

  rpc::context *context;  // 上下文对象，用于优化pb消息的生命周期和链路跟踪

  ATFW_UTIL_FORCEINLINE dispatcher_start_data_type(const dispatcher_start_data_type &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_start_data_type(dispatcher_start_data_type &&) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_start_data_type &operator=(const dispatcher_start_data_type &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_start_data_type &operator=(dispatcher_start_data_type &&) = default;

 private:
  ATFW_UTIL_FORCEINLINE dispatcher_start_data_type() noexcept
      : message(dispatcher_make_default<dispatcher_raw_message>()),
        private_data(nullptr),
        options(nullptr),
        context(nullptr) {}

  template <class T>
  friend T dispatcher_make_default() noexcept;
};

template <>
ATFW_UTIL_FORCEINLINE dispatcher_start_data_type dispatcher_make_default<dispatcher_start_data_type>() noexcept {
  return dispatcher_start_data_type();
}

template <>
ATFW_UTIL_FORCEINLINE dispatcher_resume_data_type dispatcher_make_default<dispatcher_resume_data_type>() noexcept {
  return dispatcher_resume_data_type();
}

struct ATFW_UTIL_SYMBOL_VISIBLE dispatcher_await_options {
  uint64_t sequence;
  std::chrono::system_clock::duration timeout;

  ATFW_UTIL_FORCEINLINE dispatcher_await_options(const dispatcher_await_options &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_await_options(dispatcher_await_options &&) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_await_options &operator=(const dispatcher_await_options &) = default;
  ATFW_UTIL_FORCEINLINE dispatcher_await_options &operator=(dispatcher_await_options &&) = default;

 private:
  ATFW_UTIL_FORCEINLINE dispatcher_await_options() noexcept : sequence(0), timeout(get_default_timeout()) {}

  static std::chrono::system_clock::duration get_default_timeout() noexcept;

  template <class T>
  friend T dispatcher_make_default() noexcept;
};

ATFW_UTIL_FORCEINLINE bool operator==(const dispatcher_await_options &l, const dispatcher_await_options &r) noexcept {
  return l.sequence == r.sequence;
}

namespace std {
template <>
struct ATFW_UTIL_SYMBOL_VISIBLE hash<dispatcher_await_options> {
  size_t operator()(const dispatcher_await_options &value) const noexcept { return hash<uint64_t>()(value.sequence); }
};
}  // namespace std

template <>
ATFW_UTIL_FORCEINLINE dispatcher_await_options dispatcher_make_default<dispatcher_await_options>() noexcept {
  return dispatcher_await_options();
}

class task_action_base;
class dispatcher_implement;

/**
 * @brief This function will be used to receive callback data from dispatcher/executor into variable in stack.
 *        Use the second parameter should point to a variable on stack.
 *
 */
using dispatcher_receive_start_data_callback = void (*)(const dispatcher_start_data_type *, void *);

/**
 * @brief This function will be used to receive callback data from dispatcher/executor into variable in stack.
 *        Use the second parameter should point to a variable on stack.
 *
 */
using dispatcher_receive_resume_data_callback = void (*)(const dispatcher_resume_data_type *, void *);
