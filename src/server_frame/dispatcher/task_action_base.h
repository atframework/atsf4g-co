// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler_features.h>

#include <log/log_wrapper.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_utils.h>

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

class task_action_result_type {
 public:
  using value_type = int32_t;

 private:
  template <class T>
  struct get_value_trait;

  template <class T>
  struct get_value_trait {
    inline static value_type get_value(const T &value) { return value; }
  };

  template <class T>
  struct convertiable_to_resule_type : public std::conditional<std::is_enum<T>::value || std::is_signed<T>::value,
                                                               std::true_type, std::false_type>::type {};

 public:
  explicit inline task_action_result_type(value_type code) : result_code_(code) {}
  inline task_action_result_type(const task_action_result_type &) = default;
  inline task_action_result_type(task_action_result_type &&) = default;
  inline task_action_result_type &operator=(const task_action_result_type &) = default;
  inline task_action_result_type &operator=(task_action_result_type &&) = default;

  inline operator value_type() const noexcept { return result_code_; }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline task_action_result_type &operator=(TOTHER &&other) noexcept {
    result_code_ = get_value_trait<TOTHER>::get_value(other);
    return *this;
  }

  inline friend bool operator==(const task_action_result_type &self, const task_action_result_type &other) noexcept {
    return self.result_code_ == other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator==(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ == get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator==(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) == self.result_code_;
  }

#ifdef __cpp_impl_three_way_comparison
  inline friend std::strong_ordering operator<=>(const task_action_result_type &self,
                                                 const task_action_result_type &other) noexcept {
    return self.result_code_ <=> other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend std::strong_ordering operator<=>(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ <=> get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend std::strong_ordering operator<=>(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) <=> self.result_code_;
  }
#else

  inline friend bool operator!=(const task_action_result_type &self, const task_action_result_type &other) noexcept {
    return self.result_code_ != other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator!=(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ != get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator!=(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) != self.result_code_;
  }

  inline friend bool operator<(const task_action_result_type &self, const task_action_result_type &other) noexcept {
    return self.result_code_ < other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator<(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ < get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator<(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) < self.result_code_;
  }

  inline friend bool operator<=(const task_action_result_type &self, const task_action_result_type &other) noexcept {
    return self.result_code_ <= other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator<=(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ <= get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator<=(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) <= self.result_code_;
  }

  inline friend bool operator>(const task_action_result_type &self, const task_action_result_type &other) noexcept {
    return self.result_code_ > other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator>(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ > get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator>(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) > self.result_code_;
  }

  inline friend bool operator>=(const task_action_result_type &self, const task_action_result_type &other) noexcept {
    return self.result_code_ >= other.result_code_;
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator>=(const task_action_result_type &self, TOTHER &&other) noexcept {
    return self.result_code_ >= get_value_trait<TOTHER>::get_value(other);
  }

  template <class TOTHER, class = typename convertiable_to_resule_type<typename std::decay<TOTHER>::type>::type>
  inline friend bool operator>=(TOTHER &&other, const task_action_result_type &self) noexcept {
    return get_value_trait<TOTHER>::get_value(other) >= self.result_code_;
  }
#endif

 private:
  value_type result_code_;
};

class task_action_base
#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
    : public cotask::impl::task_action_impl
#endif
{
 public:
  using result_type = task_action_result_type;
  using on_finished_callback_fn_t = std::function<void(const task_action_base &)>;
  using on_finished_callback_set_t = std::list<on_finished_callback_fn_t>;
  using on_finished_callback_handle_t = on_finished_callback_set_t::iterator;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  struct task_meta_data_type {
    task_type_trait::id_type task_id = 0;
    task_private_data_type *private_data = nullptr;
  };
#endif

 protected:
  task_action_base();
  explicit task_action_base(rpc::context *caller_context);
  virtual ~task_action_base();

 public:
  struct task_action_helper_t {
    friend class task_manager;

    static rpc::context &get_shared_context(task_action_base &action);
  };

  virtual const char *name() const;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  int operator()(task_meta_data_type &&task_meta, void *priv_data);
#else
  int operator()(void *priv_data) override;
#endif

  virtual result_type hook_run();

  virtual result_type operator()() = 0;
  inline uint64_t get_user_id() const { return user_id_; }
  inline uint32_t get_zone_id() const { return zone_id_; }

  virtual int on_success();
  virtual int on_failed();
  virtual int on_timeout();
  virtual int on_complete();

  virtual std::shared_ptr<dispatcher_implement> get_dispatcher() const = 0;
  virtual const char *get_type_name() const = 0;

  virtual rpc::context::parent_mode get_caller_mode() const noexcept;

  uint64_t get_task_id() const;

 protected:
  inline void set_user_key(uint64_t user_id, uint32_t zone_id) {
    user_id_ = user_id;
    zone_id_ = zone_id;
  }
  virtual void send_response() = 0;

 public:
  /**
   * @brief 获取逻辑返回码
   * @note 默认值为 T_APP_SUCCESS
   * @see T_APP_SUCCESS
   * @return 返回码
   */
  inline const result_type &get_result() const { return result_; }

  /**
   * @brief 获取回包返回码
   * @note 默认值为 Polar::EN_CS_SUCCESS
   * @see Polar::EN_CS_SUCCESS
   * @return 回包返回码
   */
  inline int32_t get_response_code() const { return response_code_; }

  on_finished_callback_handle_t add_on_on_finished(on_finished_callback_fn_t &&fn);
  void remove_on_finished(on_finished_callback_handle_t handle);

 protected:
  /**
   * @brief 设置逻辑返回码
   * @note 用于临时存储逻辑操作错误码
   * @param result 返回码
   */
  template <class TArg>
  inline void set_result(TArg &&result) {
    result_ = std::forward<TArg>(result);
  }

  /**
   * @brief 设置回包返回码
   * @note 用于临时存储回包返回码
   * @param rsp_code 回包返回码
   */
  inline void set_response_code(int32_t rsp_code) { response_code_ = rsp_code; }

  /**
   * @brief 禁用结束事件响应
   */
  inline void disable_finish_event() { event_disabled_ = true; }

  /**
   * @brief 开启结束事件响应
   */
  inline void enable_finish_event() { event_disabled_ = false; }

  /**
   * @brief 禁用自动回包
   */
  inline void disable_response_message() { response_message_disabled_ = true; }

  /**
   * @brief 开启自动回包
   */
  inline void enable_response_message() { response_message_disabled_ = false; }

  /**
   * @brief 获取启动透传参数
   */
  inline const dispatcher_start_data_t &get_dispatcher_start_data() const { return start_data_; }

  /**
   * @brief 获取启动透传参数
   */
  inline dispatcher_start_data_t &get_dispatcher_start_data() { return start_data_; }

  inline const rpc::context &get_shared_context() const { return shared_context_; }
  inline rpc::context &get_shared_context() { return shared_context_; }

  /**
   * @brief Set caller rpc context, we will try to reuse arena allocator and set trace parent data
   *
   * @param ctx caller context
   */
  void set_caller_context(rpc::context &ctx);

 private:
  void _notify_finished();

 private:
  uint64_t user_id_;
  uint32_t zone_id_;
  uint64_t task_id_;
  task_private_data_type *private_data_;
  result_type result_;
  int32_t response_code_;
  bool response_message_disabled_;
  bool event_disabled_;
  dispatcher_start_data_t start_data_;

  // Additional events
  on_finished_callback_set_t on_finished_callback_;

  rpc::context shared_context_;
};

template <typename TREQ>
class task_action_req_base : public task_action_base {
 public:
  using msg_type = TREQ;
  using result_type = task_action_base::result_type;

 protected:
  task_action_req_base() : request_msg_(nullptr) {}

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
struct formatter<T, typename std::enable_if<std::is_base_of<task_action_base, T>::value, char>::type>
    : formatter<std::string> {
  template <class FormatContext>
  auto format(const task_action_base &action, FormatContext &ctx) {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "task action {} [{:x}]", action.name(), action.get_task_id());
  }
};

template <class CharT>
struct formatter<task_action_result_type, CharT> : formatter<task_action_result_type::value_type, CharT> {
  template <class FormatContext>
  auto format(task_action_result_type const &val, FormatContext &ctx) -> decltype(ctx.out()) {
    return formatter<task_action_result_type::value_type, CharT>::format(
        static_cast<task_action_result_type::value_type>(val), ctx);
  }
};

}  // namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID

// When using c++20 coroutine, declare TASK_ACTION_RETURN_CODE like this
//   #define TASK_ACTION_RETURN_CODE(x) co_return (x)
#define TASK_ACTION_RETURN_CODE(x) return task_action_result_type(static_cast<task_action_result_type::value_type>(x))
