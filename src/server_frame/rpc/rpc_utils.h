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
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dispatcher/task_type_defines.h"
#include "rpc/rpc_common_types.h"
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
  using string_view = opentelemetry::nostd::string_view;
  using tracer = rpc::telemetry::tracer;
  using trace_option = rpc::telemetry::trace_option;

  enum class parent_mode : uint8_t {
    kParent = 0,
    kLink = 1,
  };

  struct inherit_options {
    parent_mode mode;
    bool inherit_allocator;

    inline inherit_options() noexcept : mode(parent_mode::kParent), inherit_allocator(true){};
    explicit inline inherit_options(parent_mode m) noexcept : mode(m), inherit_allocator(true){};
    explicit inline inherit_options(parent_mode m, bool inherit_alloc) noexcept
        : mode(m), inherit_allocator(inherit_alloc){};
  };

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
      UTIL_UNLIKELY_IF(nullptr == arena_msg_ptr_) { return &local_msg_; }

      return arena_msg_ptr_;
    }

    inline TMsg *operator->() {
      UTIL_UNLIKELY_IF(nullptr == arena_msg_ptr_) { return &local_msg_; }

      return arena_msg_ptr_;
    }

    inline const TMsg &operator*() const {
      UTIL_UNLIKELY_IF(nullptr == arena_msg_ptr_) { return local_msg_; }

      return *arena_msg_ptr_;
    }

    inline TMsg &operator*() {
      UTIL_UNLIKELY_IF(nullptr == arena_msg_ptr_) { return local_msg_; }

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
  context() noexcept;
  explicit context(context &&other) noexcept;
  explicit context(context &parent, inherit_options options = {}) noexcept;
  ~context();

  /**
   * @brief 创建临时的子上下文，通常用于协程栈上需要加一层链路跟踪。
   * @note 如果涉及异步调用和传递生命周期，请使用 create_shared_child
   *
   * @param options 继承选项
   * @return context 创建的子上下文对象
   */
  context create_temporary_child(inherit_options options = {}) noexcept;

  /**
   * @brief 创建共享的子上下文，通常用于异步调用时保留链路信息。
   * @note 如果不涉及异步调用和传递生命周期，请使用 create_temporary_child
   *
   * @param options 继承选项
   * @return std::shared_ptr<context> 创建的子上下文智能指针
   */
  std::shared_ptr<context> create_shared_child(inherit_options options = {}) noexcept;

  void setup_tracer(
      tracer &, string_view name, trace_option &&options,
      std::initializer_list<std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>>
          attributes = {});

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
  bool try_reuse_protobuf_arena(const std::shared_ptr<::google::protobuf::Arena> &arena) noexcept;

  inline const tracer::span_ptr_type &get_trace_span() const { return trace_span_; }

  void set_parent_context(rpc::context &parent, inherit_options options = {}) noexcept;
  void add_link_span(const tracer::span_ptr_type &span_ptr) noexcept;

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
  parent_mode parent_mode_;
  std::vector<tracer::span_ptr_type> link_spans_;
};

/**
 * @brief sleep and wait a moment
 *
 * @param timeout
 * @return future of 0 or error code
 */
result_code_type wait(context &ctx, std::chrono::system_clock::duration timeout);

/**
 * @brief sleep and wait a moment
 *
 * @tparam Rep
 * @tparam Period
 * @param timeout
 * @return future of 0 or error code
 */
template <class Rep, class Period>
inline result_code_type wait(context &ctx, std::chrono::duration<Rep, Period> timeout) {
  return wait(ctx, std::chrono::duration_cast<std::chrono::system_clock::duration>(timeout));
}

result_code_type wait(atframework::SSMsg &msg, uint64_t check_sequence);
result_code_type wait(PROJECT_NAMESPACE_ID::table_all_message &msg, uint64_t check_sequence);

/**
 * @brief wait for multiple messages
 *
 * @param waiters sequences of waiting messages
 * @param received received messages
 * @param wakeup_count wakeup and return after got this count of messages(0 means wait all)
 * @return future of 0 or error code
 */
result_code_type wait(const std::unordered_set<uint64_t> &waiters,
                      std::unordered_map<uint64_t, atframework::SSMsg> &received, size_t wakeup_count = 0);

/**
 * @brief wait for multiple messages
 *
 * @param waiters sequences of waiting messages
 * @param received received messages
 * @param wakeup_count wakeup and return after got this count of messages(0 means wait all)
 * @return future of 0 or error code
 */
result_code_type wait(const std::unordered_set<uint64_t> &waiters,
                      std::unordered_map<uint64_t, atframework::SSMsg *> &received, size_t wakeup_count = 0);

/**
 * @brief Custom wait for a message or resume
 *
 * @param type_address type object address, user should keep it unique for each message type
 * @param received where to store received data, pass nullptr to ignore it
 * @param check_sequence check sequence for this message type
 * @return future of 0 or error code
 */
result_code_type custom_wait(const void *type_address, void **received, uint64_t check_sequence);

/**
 * @brief Custom resume a waiter
 *
 * @param task task to resume
 * @param type_address type object address, user should keep it unique for each message type
 * @param sequence sequence for this message type and this resume
 * @param received this will be assigned received in custom_wait
 * @return future of 0 or error code
 */
int32_t custom_resume(task_types::task_type &task, const void *type_address, uint64_t sequence, void *received);
}  // namespace rpc
