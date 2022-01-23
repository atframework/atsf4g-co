// Copyright 2021 atframework
// Created by owent on 2016/10/4.
//

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>
#include <design_pattern/result_type.h>
#include <gsl/select-gsl.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/arena.h>
#include <google/protobuf/timestamp.pb.h>

#include <protocol/pbdesc/atframework.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/server_frame_build_feature.h>

#include <opentelemetry/trace/span.h>

#include <stdint.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rpc/telemetry/rpc_trace.h"

namespace atapp {
class app;
}

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_telemetry_cfg;
}

class table_all_message;
PROJECT_NAMESPACE_END

namespace atframework {
class SSMsg;
}

namespace rpc {
class context {
 public:
  using string_view = ::opentelemetry::nostd::string_view;
  using tracer = ::rpc::telemetry::tracer;
  using trace_option = ::rpc::telemetry::trace_option;

  template <class TMsg>
  struct message_holder {
    explicit message_holder(context &ctx) : arena_msg_ptr_(ctx.create<TMsg>()) {}
    explicit message_holder(message_holder &&other) : arena_msg_ptr_(nullptr) {
      using std::swap;

      if (other->GetArena() != nullptr) {
        arena_msg_ptr_ = ::google::protobuf::Arena::CreateMessage<TMsg>(other->GetArena());
      }
      swap(arena_msg_ptr_, other.arena_msg_ptr_);
      local_msg_.Swap(&other.local_msg_);
    }

    message_holder &operator=(message_holder &&other) {
      using std::swap;

      swap(arena_msg_ptr_, other.arena_msg_ptr_);
      local_msg_.Swap(&other.local_msg_);

      return *this;
    }

    inline const TMsg *operator->() const {
      if (unlikely(nullptr == arena_msg_ptr_)) {
        return &local_msg_;
      }

      return arena_msg_ptr_;
    }

    inline TMsg *operator->() {
      if (unlikely(nullptr == arena_msg_ptr_)) {
        return &local_msg_;
      }

      return arena_msg_ptr_;
    }

    inline const TMsg &operator*() const {
      if (unlikely(nullptr == arena_msg_ptr_)) {
        return local_msg_;
      }

      return *arena_msg_ptr_;
    }

    inline TMsg &operator*() {
      if (unlikely(nullptr == arena_msg_ptr_)) {
        return local_msg_;
      }

      return *arena_msg_ptr_;
    }

    UTIL_DESIGN_PATTERN_NOCOPYABLE(message_holder)

   private:
    TMsg *arena_msg_ptr_;
    TMsg local_msg_;
  };

 private:
  context(const context &) = delete;
  context &operator=(const context &) = delete;
  context &operator=(context &&) = delete;

 public:
  context();
  explicit context(context &&other);
  explicit context(context &parent);
  ~context();

  void setup_tracer(tracer &, string_view name, trace_option &&options);

  /**
   * @brief 使用内置的Arena创建protobuf对象。注意，该对象必须是局部变量，不允许转移给外部使用
   *
   * @tparam message类型
   * @return 在arena上分配的对象，失败返回nullptr
   */
  template <class TMSG>
  TMSG *create() {
    // 上面的分支减少一次atomic操作
    if (allocator_) {
      return ::google::protobuf::Arena::CreateMessage<TMSG>(allocator_.get());
    }

    auto arena = mutable_protobuf_arena();
    if (!arena) {
      return nullptr;
    }

    return ::google::protobuf::Arena::CreateMessage<TMSG>(arena.get());
  }

  std::shared_ptr<::google::protobuf::Arena> mutable_protobuf_arena();
  const std::shared_ptr<::google::protobuf::Arena> &get_protobuf_arena() const;
  bool try_reuse_protobuf_arena(const std::shared_ptr<::google::protobuf::Arena> &arena);

  inline const tracer::span_ptr_type &get_trace_span() const { return trace_span_; }
  void set_parent_context(rpc::context &parent);

  /**
   * @brief Set the current service object, it's used for tracer
   * @param app atapp instance
   * @param telemetry telemetry configure
   */
  static void set_current_service(atapp::app &app, const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &telemetry);

 private:
  std::shared_ptr<::google::protobuf::Arena> allocator_;
  tracer::span_ptr_type trace_span_;
  tracer::span_ptr_type parent_span_;
};

int wait(atframework::SSMsg &msg, uint64_t check_sequence);
int wait(PROJECT_NAMESPACE_ID::table_all_message &msg, uint64_t check_sequence);
int wait(std::unordered_map<uint64_t, atframework::SSMsg> &msg_waiters);
int wait(std::unordered_map<uint64_t, atframework::SSMsg *> &msg_waiters);
}  // namespace rpc
