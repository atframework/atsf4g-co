// Copyright 2021 atframework
// Created by owent on 2018-04-06.
//

#ifndef DISPATCHER_DISPATCHER_TYPE_DEFINES_H
#define DISPATCHER_DISPATCHER_TYPE_DEFINES_H

#pragma once

#include <stdint.h>
#include <cstddef>
#include <cstdlib>

#include <config/compiler_features.h>
#include <std/explicit_declare.h>

namespace atframework {
class DispatcherOptions;
class RpcTraceSpan;
}  // namespace atframework

namespace rpc {
class context;
}

template <class T>
T dispatcher_make_default();

struct dispatcher_raw_message {
  uintptr_t msg_type;  // 建议对所有的消息体类型分配一个ID，用以检查回调类型转换。推荐时使用dispatcher单例的地址。
  void *msg_addr;

 private:
  dispatcher_raw_message() : msg_type(0), msg_addr(nullptr) {}

  template <class T>
  friend T dispatcher_make_default();
};

template <>
inline dispatcher_raw_message dispatcher_make_default<dispatcher_raw_message>() {
  return dispatcher_raw_message();
}

struct dispatcher_resume_data_t {
  dispatcher_raw_message message;  // 异步回调中用于透传消息体
  void *private_data;              // 异步回调中用于透传额外的私有数据
  uint64_t sequence;  // 等待序号，通常和发送序号匹配。用于检测同类消息是否是发出的那个

  rpc::context *context;  // 上下文对象，用于优化pb消息的生命周期和链路跟踪

 private:
  dispatcher_resume_data_t()
      : message(dispatcher_make_default<dispatcher_raw_message>()),
        private_data(nullptr),
        sequence(0),
        context(nullptr) {}

  template <class T>
  friend T dispatcher_make_default();
};

struct dispatcher_start_data_t {
  dispatcher_raw_message message;                 // 启动回调中用于透传消息体
  void *private_data;                             // 启动回调中用于透传额外的私有数据
  const atframework::DispatcherOptions *options;  // 调度协议层选项

  rpc::context *context;  // 上下文对象，用于优化pb消息的生命周期和链路跟踪

 private:
  dispatcher_start_data_t()
      : message(dispatcher_make_default<dispatcher_raw_message>()),
        private_data(nullptr),
        options(nullptr),
        context(nullptr) {}

  template <class T>
  friend T dispatcher_make_default();
};

template <>
inline dispatcher_start_data_t dispatcher_make_default<dispatcher_start_data_t>() {
  return dispatcher_start_data_t();
}

template <>
inline dispatcher_resume_data_t dispatcher_make_default<dispatcher_resume_data_t>() {
  return dispatcher_resume_data_t();
}

class task_action_base;
class actor_action_base;
class dispatcher_implement;

#endif  // DISPATCHER_DISPATCHER_TYPE_DEFINES_H
