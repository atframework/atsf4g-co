// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler_features.h>

#include <log/log_wrapper.h>

#include <memory/object_allocator.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_common_types.h>
#include <rpc/rpc_context.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#ifdef __cpp_impl_three_way_comparison
#  include <compare>
#endif

#include "dispatcher/dispatcher_type_defines.h"
#include "dispatcher/task_type_traits.h"

class player_cache;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
using task_action_result_type = rpc::result_code_type;
#else
class ATFW_UTIL_SYMBOL_VISIBLE task_action_result_type {
 public:
  using value_type = int32_t;

 private:
  template <class T>
  struct get_value_trait;

  template <class T>
  struct get_value_trait {
    ATFW_UTIL_FORCEINLINE static value_type get_value(const T &value) { return value; }
  };

  template <class T>
  struct convertiable_to_resule_type : public std::conditional<std::is_enum<T>::value || std::is_signed<T>::value,
                                                               std::true_type, std::false_type>::type {};

 public:
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  ATFW_UTIL_FORCEINLINE void _internal_set_awaited() noexcept {}
#  endif

  ATFW_UTIL_FORCEINLINE explicit task_action_result_type(value_type code) : result_code_(code) {}
  ATFW_UTIL_FORCEINLINE task_action_result_type(const task_action_result_type &) = default;
  ATFW_UTIL_FORCEINLINE task_action_result_type(task_action_result_type &&) = default;
  ATFW_UTIL_FORCEINLINE task_action_result_type &operator=(const task_action_result_type &) = default;
  ATFW_UTIL_FORCEINLINE task_action_result_type &operator=(task_action_result_type &&) = default;

  ATFW_UTIL_FORCEINLINE operator value_type() const noexcept { return result_code_; }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE task_action_result_type &operator=(TOTHER &&other) noexcept {
    result_code_ = get_value_trait<TOTHER>::get_value(other);
    return *this;
  }

  ATFW_UTIL_FORCEINLINE friend bool operator==(const task_action_result_type &self,
                                               const task_action_result_type &other) noexcept {
    return self.result_code_ == other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator==(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ == get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator==(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) == self.result_code_;
  }

#  ifdef __cpp_impl_three_way_comparison
  ATFW_UTIL_FORCEINLINE friend std::strong_ordering operator<=>(const task_action_result_type &self,
                                                                const task_action_result_type &other) noexcept {
    return self.result_code_ <=> other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend std::strong_ordering operator<=>(const task_action_result_type &self,
                                                                TOTHER &&other) noexcept {
    return self.result_code_ <=> get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend std::strong_ordering operator<=>(TOTHER &&other,
                                                                const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) <=> self.result_code_;
  }
#  else

  ATFW_UTIL_FORCEINLINE friend bool operator!=(const task_action_result_type &self,
                                               const task_action_result_type &other) noexcept {
    return self.result_code_ != other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator!=(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ != get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator!=(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) != self.result_code_;
  }

  ATFW_UTIL_FORCEINLINE friend bool operator<(const task_action_result_type &self,
                                              const task_action_result_type &other) noexcept {
    return self.result_code_ < other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator<(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ < get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator<(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) < self.result_code_;
  }

  ATFW_UTIL_FORCEINLINE friend bool operator<=(const task_action_result_type &self,
                                               const task_action_result_type &other) noexcept {
    return self.result_code_ <= other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator<=(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ <= get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator<=(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) <= self.result_code_;
  }

  ATFW_UTIL_FORCEINLINE friend bool operator>(const task_action_result_type &self,
                                              const task_action_result_type &other) noexcept {
    return self.result_code_ > other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator>(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ > get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator>(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) > self.result_code_;
  }

  ATFW_UTIL_FORCEINLINE friend bool operator>=(const task_action_result_type &self,
                                               const task_action_result_type &other) noexcept {
    return self.result_code_ >= other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator>=(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ >= get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  ATFW_UTIL_FORCEINLINE friend bool operator>=(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) >= self.result_code_;
  }
#  endif

 private:
  value_type result_code_;
};
#endif

class ATFW_UTIL_SYMBOL_VISIBLE task_action_base
#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
    : public cotask::impl::task_action_impl
#endif
{
 public:
  using result_type = task_action_result_type;
  using on_finished_callback_fn_t = std::function<void(const task_action_base &)>;
  using on_finished_callback_set_t = std::list<on_finished_callback_fn_t>;
  using on_finished_callback_handle_t = on_finished_callback_set_t::iterator;

 protected:
  SERVER_FRAME_API task_action_base(const dispatcher_start_data_type &start_param);
  SERVER_FRAME_API virtual ~task_action_base();

 public:
  struct task_action_helper_t {
    friend class task_manager;

    SERVER_FRAME_API static rpc::context &get_shared_context(task_action_base &action);
  };

  SERVER_FRAME_API virtual const char *name() const;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  SERVER_FRAME_API result_type operator()(task_action_meta_data_type &&task_meta,
                                          dispatcher_start_data_type &&start_data);
#else
  SERVER_FRAME_API int operator()(void *priv_data) override;
#endif

  SERVER_FRAME_API virtual result_type hook_run();

  virtual result_type operator()() = 0;
  ATFW_UTIL_FORCEINLINE uint64_t get_user_id() const { return user_id_; }
  ATFW_UTIL_FORCEINLINE uint32_t get_zone_id() const { return zone_id_; }

  SERVER_FRAME_API virtual int on_success();
  SERVER_FRAME_API virtual int on_failed();
  SERVER_FRAME_API virtual int on_timeout();
  SERVER_FRAME_API virtual int on_complete();

  virtual std::shared_ptr<dispatcher_implement> get_dispatcher() const = 0;
  virtual const char *get_type_name() const = 0;

  SERVER_FRAME_API virtual rpc::context::inherit_options get_inherit_option() const noexcept;
  SERVER_FRAME_API virtual rpc::context::trace_start_option get_trace_option() const noexcept;

  SERVER_FRAME_API uint64_t get_task_id() const;

 protected:
  SERVER_FRAME_API void set_user_key(uint64_t user_id, uint32_t zone_id);
  virtual void send_response() = 0;

 public:
  /**
   * @brief 获取逻辑返回码
   * @note 默认值为 T_APP_SUCCESS
   * @see T_APP_SUCCESS
   * @return 返回码
   */
  ATFW_UTIL_FORCEINLINE int32_t get_result() const noexcept { return result_; }

  /**
   * @brief 获取回包返回码
   * @note 默认值为 Polar::EN_CS_SUCCESS
   * @see Polar::EN_CS_SUCCESS
   * @return 回包返回码
   */
  ATFW_UTIL_FORCEINLINE int32_t get_response_code() const noexcept { return response_code_; }

  /**
   * @brief 获取外部模块返回码
   * @return 回包返回码
   */
  ATFW_UTIL_FORCEINLINE int64_t get_external_response_code() const noexcept { return external_error_code_; }

  /**
   * @brief 获取外部模块返回消息
   * @return 回包返回码
   */
  ATFW_UTIL_FORCEINLINE const std::string &get_external_response_message() const noexcept {
    return external_error_message_;
  }

  SERVER_FRAME_API on_finished_callback_handle_t add_on_finished(on_finished_callback_fn_t &&fn);
  SERVER_FRAME_API void remove_on_finished(on_finished_callback_handle_t handle);

 protected:
  /**
   * @brief 设置逻辑返回码
   * @note 用于临时存储逻辑操作错误码
   * @param result 返回码
   */
  template <class TArg>
  ATFW_UTIL_FORCEINLINE void set_result(TArg &&result) {
    result_ = std::forward<TArg>(result);
  }

  /**
   * @brief 设置回包返回码
   * @note 用于临时存储回包返回码
   * @param rsp_code 回包返回码
   * @param external_error_code 外部模块返回码
   * @param external_error_message 外部模块返回消息
   */
  SERVER_FRAME_API void set_response_code(int32_t rsp_code, int64_t external_error_code = 0,
                                          gsl::string_view external_error_message = {});

  /**
   * @brief 禁用结束事件响应
   */
  ATFW_UTIL_FORCEINLINE void disable_finish_event() { event_disabled_ = true; }

  /**
   * @brief 开启结束事件响应
   */
  ATFW_UTIL_FORCEINLINE void enable_finish_event() { event_disabled_ = false; }

  /**
   * @brief 禁用自动回包
   */
  ATFW_UTIL_FORCEINLINE void disable_response_message() { response_message_disabled_ = true; }

  /**
   * @brief 开启自动回包
   */
  ATFW_UTIL_FORCEINLINE void enable_response_message() { response_message_disabled_ = false; }

  /**
   * @brief 是否开启了自动回包
   */
  ATFW_UTIL_FORCEINLINE bool is_response_message_enabled() const noexcept { return !response_message_disabled_; }

  /**
   * @brief 获取调度层设置
   *
   * @return const atframework::DispatcherOptions*
   */
  ATFW_UTIL_FORCEINLINE const atframework::DispatcherOptions *get_dispatcher_options() noexcept {
    return dispatcher_options_;
  }

  ATFW_UTIL_FORCEINLINE const rpc::context &get_shared_context() const { return shared_context_; }
  ATFW_UTIL_FORCEINLINE rpc::context &get_shared_context() { return shared_context_; }

  /**
   * @brief Set caller rpc context, we will try to reuse arena allocator and set trace parent data
   *
   * @param ctx caller context
   */
  SERVER_FRAME_API void set_caller_context(rpc::context &ctx);

 private:
  enum class trace_attribute_type : size_t {
    kRpcSystem = 0,
    kRpcService = 1,
    kRpcMethod = 2,
    kAtRpcKind = 3,
    kAtRpcSpanName = 4,
    kTaskResponseCode = 5,
    kMax = 6,
  };
  using task_trace_attributes =
      rpc::telemetry::trace_attribute_pair_type[static_cast<size_t>(trace_attribute_type::kMax)];
  result_type::value_type _notify_finished(int32_t final_result, rpc::context::tracer &tracer,
                                           task_trace_attributes &attributes);

 private:
  uint64_t user_id_;
  uint32_t zone_id_;
  task_private_data_type *private_data_;
  int32_t result_;
  int32_t response_code_;
  bool response_message_disabled_;
  bool event_disabled_;

  int64_t external_error_code_;
  std::string external_error_message_;

  const atframework::DispatcherOptions *dispatcher_options_;

  // Additional events
  on_finished_callback_set_t on_finished_callback_;

  rpc::context shared_context_;
};

template <typename TREQ>
class ATFW_UTIL_SYMBOL_VISIBLE task_action_req_base : public task_action_base {
 public:
  using message_type = TREQ;
  using result_type = task_action_base::result_type;

 protected:
  explicit task_action_req_base(const dispatcher_start_data_type &start_param)
      : task_action_base(start_param), request_msg_(nullptr) {}

  inline TREQ &get_request() {
    if (nullptr != request_msg_) {
      return *request_msg_;
    }

    request_msg_ = get_shared_context().template create<TREQ>();
    if (nullptr != request_msg_) {
      return *request_msg_;
    }

    static TREQ empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

  inline const TREQ &get_request() const {
    if (nullptr != request_msg_) {
      return *request_msg_;
    }

    request_msg_ = const_cast<rpc::context &>(get_shared_context()).create<TREQ>();
    if (nullptr != request_msg_) {
      return *request_msg_;
    }

    static TREQ empty_msg;
    empty_msg.Clear();
    return empty_msg;
  }

 private:
  mutable TREQ *request_msg_;
};

namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID {
template <class T>
struct ATFW_UTIL_SYMBOL_VISIBLE
formatter<T, typename std::enable_if<std::is_base_of<task_action_base, T>::value, char>::type>
    : formatter<std::string> {
  template <class FormatContext>
  auto format(const task_action_base &action, FormatContext &ctx) {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "task action {} [{:x}]", action.name(), action.get_task_id());
  }
};

}  // namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  define TASK_ACTION_RETURN_CODE(x) co_return (x)
#else
#  define TASK_ACTION_RETURN_CODE(x) return task_action_result_type(static_cast<task_action_result_type::value_type>(x))
#endif
