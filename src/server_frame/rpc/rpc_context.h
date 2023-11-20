// Copyright 2023 atframework
// Created by owent on 2023-09-07.
//

#pragma once

#include <design_pattern/noncopyable.h>
#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>

#include <config/compile_optimize.h>

#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/trace_id.h>

#include <config/server_frame_build_feature.h>

#include "rpc/rpc_common_types.h"
#include "rpc/telemetry/rpc_trace.h"

namespace atapp {
class app;
}

PROJECT_NAMESPACE_BEGIN
namespace config {
class logic_telemetry_cfg;
}
PROJECT_NAMESPACE_END

namespace rpc {

class context {
 public:
  using string_view = opentelemetry::nostd::string_view;
  using tracer = rpc::telemetry::tracer;
  using trace_start_option = rpc::telemetry::trace_start_option;

  enum class parent_mode : uint8_t {
    kParent = 0,
    kLink = 1,
  };

  struct UTIL_SYMBOL_VISIBLE inherit_options {
    parent_mode mode;
    bool inherit_allocator;
    bool inherit_parent_span;

    inline inherit_options() noexcept
        : mode(parent_mode::kParent), inherit_allocator(true), inherit_parent_span(true) {}
    explicit inline inherit_options(parent_mode m) noexcept
        : mode(m), inherit_allocator(true), inherit_parent_span(true) {}
    explicit inline inherit_options(parent_mode m, bool inherit_alloc) noexcept
        : mode(m), inherit_allocator(inherit_alloc), inherit_parent_span(true) {}
    explicit inline inherit_options(parent_mode m, bool inherit_alloc, bool inherit_parent_trace_span) noexcept
        : mode(m), inherit_allocator(inherit_alloc), inherit_parent_span(inherit_parent_trace_span) {}
  };

  struct UTIL_SYMBOL_VISIBLE create_options {};

  struct UTIL_SYMBOL_VISIBLE task_context_data {
    uint64_t task_id;
    gsl::string_view task_name;
    uint32_t reference_object_type_id;
    uint32_t reference_object_zone_id;
    uint64_t reference_object_instance_id;
    inline task_context_data() noexcept
        : task_id(0), reference_object_type_id(0), reference_object_zone_id(0), reference_object_instance_id(0) {}
  };

  template <class TMsg>
  struct UTIL_SYMBOL_VISIBLE message_holder {
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

  SERVER_FRAME_API context() noexcept;

 public:
  SERVER_FRAME_API explicit context(context &&other) noexcept;
  SERVER_FRAME_API context(context &parent, inherit_options options = {}) noexcept;
  SERVER_FRAME_API ~context();

  /**
   * @brief 创建和任务无关的RPC上下文，通常用于记录链路跟踪关系。
   *
   * @param options 创建选项
   * @return context 创建的子上下文对象
   */
  SERVER_FRAME_API static context create_without_task(create_options options = {}) noexcept;

  /**
   * @brief 创建临时的子上下文，通常用于协程栈上需要加一层链路跟踪。
   * @note 如果涉及异步调用和传递生命周期，请使用 create_shared_child
   *
   * @param options 继承选项
   * @return context 创建的子上下文对象
   */
  SERVER_FRAME_API context create_temporary_child(inherit_options options = {}) noexcept;

  /**
   * @brief 创建共享的子上下文，通常用于异步调用时保留链路信息。
   * @note 如果不涉及异步调用和传递生命周期，请使用 create_temporary_child
   *
   * @param options 继承选项
   * @return std::shared_ptr<context> 创建的子上下文智能指针
   */
  SERVER_FRAME_API std::shared_ptr<context> create_shared_child(inherit_options options = {}) noexcept;

  SERVER_FRAME_API void setup_tracer(tracer &, string_view name, trace_start_option &&options);

  /**
   * @brief 创建链路跟踪器信息。
   * @note 如果不涉及异步调用和传递生命周期，请使用 create_temporary_child
   *
   * @param name 链路跟踪名称
   * @param options 继承选项
   * @param attributes 自定义属性
   * @return 链路跟踪器
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API tracer make_tracer(string_view name, trace_start_option &&options);

  /**
   * @brief 使用内置的Arena创建protobuf对象。注意，该对象必须是局部变量，不允许转移给外部使用
   *
   * @tparam message类型
   * @return 在arena上分配的对象，失败返回nullptr
   */
  template <class TMSG>
  UTIL_SYMBOL_VISIBLE TMSG *create() {
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

  SERVER_FRAME_API std::shared_ptr<::google::protobuf::Arena> mutable_protobuf_arena();
  SERVER_FRAME_API const std::shared_ptr<::google::protobuf::Arena> &get_protobuf_arena() const;
  SERVER_FRAME_API bool try_reuse_protobuf_arena(const std::shared_ptr<::google::protobuf::Arena> &arena) noexcept;

  SERVER_FRAME_API const tracer::span_ptr_type &get_trace_span() const noexcept;

  SERVER_FRAME_API void set_parent_context(rpc::context &parent, inherit_options options = {}) noexcept;
  SERVER_FRAME_API void add_link_span(const tracer::span_ptr_type &span_ptr) noexcept;

  /**
   * @brief Set the current service object, it's used for tracer
   * @param app atapp instance
   * @param telemetry telemetry configure
   */
  SERVER_FRAME_API static void set_current_service(atapp::app &app,
                                                   const PROJECT_NAMESPACE_ID::config::logic_telemetry_cfg &telemetry);

  SERVER_FRAME_API void set_task_context(const task_context_data &task_ctx) noexcept;
  UTIL_FORCEINLINE const task_context_data &get_task_context() const noexcept { return task_context_; }
  UTIL_FORCEINLINE void update_task_context_reference_object(uint32_t type_id, uint32_t zone_id,
                                                             uint64_t instance_id) noexcept {
    task_context_.reference_object_type_id = type_id;
    task_context_.reference_object_zone_id = zone_id;
    task_context_.reference_object_instance_id = instance_id;
  }

 private:
  std::shared_ptr<::google::protobuf::Arena> allocator_;
  struct trace_context_data {
    tracer::span_ptr_type trace_span;
    tracer::span_ptr_type parent_span;
    parent_mode caller_mode;
    std::vector<tracer::span_ptr_type> link_spans;
  };
  trace_context_data trace_context_;
  task_context_data task_context_;
};
}  // namespace rpc

namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID {
template <class CharT>
struct formatter<rpc::context, CharT> : formatter<std::string, CharT> {
  template <class FormatContext>
  auto format(const rpc::context &rpc_ctx, FormatContext &fmt_ctx) const {
    auto ret = LOG_WRAPPER_FWAPI_FORMAT_TO(fmt_ctx.out(), ": task_id={}", rpc_ctx.get_task_context().task_id);
    if (!rpc_ctx.get_task_context().task_name.empty()) {
      ret = LOG_WRAPPER_FWAPI_FORMAT_TO(ret, ", task_name={}", rpc_ctx.get_task_context().task_name);
    }

    if (rpc_ctx.get_task_context().reference_object_type_id != 0) {
      ret = LOG_WRAPPER_FWAPI_FORMAT_TO(ret, ", object_type={}", rpc_ctx.get_task_context().reference_object_type_id);
    }
    if (rpc_ctx.get_task_context().reference_object_zone_id != 0) {
      ret = LOG_WRAPPER_FWAPI_FORMAT_TO(ret, ", zone_id={}", rpc_ctx.get_task_context().reference_object_zone_id);
    }
    if (rpc_ctx.get_task_context().reference_object_instance_id != 0) {
      ret = LOG_WRAPPER_FWAPI_FORMAT_TO(ret, ", object_instance_id={}",
                                        rpc_ctx.get_task_context().reference_object_instance_id);
    }
    const ::rpc::context::tracer::span_ptr_type &trace_span = rpc_ctx.get_trace_span();
    if (trace_span) {
      auto trace_ctx = trace_span->GetContext();
      char trace_id_buffer[static_cast<size_t>(opentelemetry::trace::TraceId::kSize) * 2];
      char span_id_buffer[static_cast<size_t>(opentelemetry::trace::SpanId::kSize) * 2];
      trace_ctx.trace_id().ToLowerBase16(trace_id_buffer);
      trace_ctx.span_id().ToLowerBase16(span_id_buffer);
      ret = LOG_WRAPPER_FWAPI_FORMAT_TO(ret, ", trace_id={}, span_id={}",
                                        gsl::string_view{trace_id_buffer, sizeof(trace_id_buffer)},
                                        gsl::string_view{span_id_buffer, sizeof(span_id_buffer)});
    }
    ret = LOG_WRAPPER_FWAPI_FORMAT_TO(ret, "", "\n\t");
    return ret;
  }
};
}  // namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID

// 玩家日志输出工具
#ifdef _MSC_VER
#  define FCTXLOGTRACE(__CTX, fmt, ...) FWLOGTRACE("{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXLOGDEBUG(__CTX, fmt, ...) FWLOGDEBUG("{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXLOGNOTICE(__CTX, fmt, ...) FWLOGNOTICE("{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXLOGINFO(__CTX, fmt, ...) FWLOGINFO("{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXLOGWARNING(__CTX, fmt, ...) FWLOGWARNING("{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXLOGERROR(__CTX, fmt, ...) FWLOGERROR("{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXLOGFATAL(__CTX, fmt, ...) FWLOGFATAL("{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXCLOGTRACE(__CAT, __CTX, fmt, ...) FWCLOGTRACE(__CAT, "{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXCLOGDEBUG(__CAT, __CTX, fmt, ...) FWCLOGDEBUG(__CAT, "{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXCLOGNOTICE(__CAT, __CTX, fmt, ...) FWCLOGNOTICE(__CAT, "{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXCLOGINFO(__CAT, __CTX, fmt, ...) FWCLOGINFO(__CAT, "{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXCLOGWARNING(__CAT, __CTX, fmt, ...) FWCLOGWARNING(__CAT, "{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXCLOGERROR(__CAT, __CTX, fmt, ...) FWCLOGERROR(__CAT, "{}" fmt, (__CTX), __VA_ARGS__)
#  define FCTXCLOGFATAL(__CAT, __CTX, fmt, ...) FWCLOGFATAL(__CAT, "{}" fmt, (__CTX), __VA_ARGS__)
#else
#  define FCTXLOGTRACE(__CTX, fmt, args...) FWLOGTRACE("{}" fmt, (__CTX), ##args)
#  define FCTXLOGDEBUG(__CTX, fmt, args...) FWLOGDEBUG("{}" fmt, (__CTX), ##args)
#  define FCTXLOGNOTICE(__CTX, fmt, args...) FWLOGNOTICE("{}" fmt, (__CTX), ##args)
#  define FCTXLOGINFO(__CTX, fmt, args...) FWLOGINFO("{}" fmt, (__CTX), ##args)
#  define FCTXLOGWARNING(__CTX, fmt, args...) FWLOGWARNING("{}" fmt, (__CTX), ##args)
#  define FCTXLOGERROR(__CTX, fmt, args...) FWLOGERROR("{}" fmt, (__CTX), ##args)
#  define FCTXLOGFATAL(__CTX, fmt, args...) FWLOGFATAL("{}" fmt, (__CTX), ##args)
#  define FCTXCLOGTRACE(__CAT, __CTX, fmt, args...) FWCLOGTRACE(__CAT, "{}" fmt, (__CTX), ##args)
#  define FCTXCLOGDEBUG(__CAT, __CTX, fmt, args...) FWCLOGDEBUG(__CAT, "{}" fmt, (__CTX), ##args)
#  define FCTXCLOGNOTICE(__CAT, __CTX, fmt, args...) FWCLOGNOTICE(__CAT, "{}" fmt, (__CTX), ##args)
#  define FCTXCLOGINFO(__CAT, __CTX, fmt, args...) FWCLOGINFO(__CAT, "{}" fmt, (__CTX), ##args)
#  define FCTXCLOGWARNING(__CAT, __CTX, fmt, args...) FWCLOGWARNING(__CAT, "{}" fmt, (__CTX), ##args)
#  define FCTXCLOGERROR(__CAT, __CTX, fmt, args...) FWCLOGERROR(__CAT, "{}" fmt, (__CTX), ##args)
#  define FCTXCLOGFATAL(__CAT, __CTX, fmt, args...) FWCLOGFATAL(__CAT, "{}" fmt, (__CTX), ##args)
#endif
