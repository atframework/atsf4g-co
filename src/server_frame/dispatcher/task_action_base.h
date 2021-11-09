// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#pragma once

#include <config/compiler_features.h>

#include <libcopp/utils/features.h>
#include <libcotask/task.h>

#include <log/log_wrapper.h>

#include <rpc/rpc_utils.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include "dispatcher/dispatcher_type_defines.h"

/**
 * action 默认结构
 * action rpc 接口
 * action rpc.1 记录+Dispatcher发送接口（出错则直接返回）
 * action rpc.2 检查回包+填充rsp包+返回调用者
 *
 * action rpc 启动（填充初始包+ operator()(void*) => operator()() ）
 */

class player_cache;

class task_action_base : public ::cotask::impl::task_action_impl {
 public:
  using result_type = int;
  using on_finished_callback_fn_t = std::function<void(const task_action_base &)>;
  using on_finished_callback_set_t = std::list<on_finished_callback_fn_t>;
  using on_finished_callback_handle_t = on_finished_callback_set_t::iterator;

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

  int operator()(void *priv_data);
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
  virtual const atframework::RpcTraceSpan *get_parent_trace_span() const;

  uint64_t get_task_id() const;

 protected:
  inline void set_user_key(uint64_t user_id, uint32_t zone_id) {
    user_id_ = user_id;
    zone_id_ = zone_id;
  }
  virtual void send_rsp_msg() = 0;

 public:
  /**
   * @brief 获取逻辑返回码
   * @note 默认值为 T_APP_SUCCESS
   * @see T_APP_SUCCESS
   * @return 返回码
   */
  inline const result_type &get_ret_code() const { return ret_code_; }

  /**
   * @brief 获取回包返回码
   * @note 默认值为 Polar::EN_CS_SUCCESS
   * @see Polar::EN_CS_SUCCESS
   * @return 回包返回码
   */
  inline int32_t get_rsp_code() const { return rsp_code_; }

  on_finished_callback_handle_t add_on_on_finished(on_finished_callback_fn_t &&fn);
  void remove_on_finished(on_finished_callback_handle_t handle);

 protected:
  /**
   * @brief 设置逻辑返回码
   * @note 用于临时存储逻辑操作错误码
   * @param ret_code 返回码
   */
  template <class TArg>
  inline void set_ret_code(TArg &&ret_code) {
    ret_code_ = std::forward<TArg>(ret_code);
  }

  /**
   * @brief 设置回包返回码
   * @note 用于临时存储回包返回码
   * @param rsp_code 回包返回码
   */
  inline void set_rsp_code(int32_t rsp_code) { rsp_code_ = rsp_code; }

  /**
   * @brief 禁用结束事件响应
   */
  inline void disable_finish_evt() { evt_disabled_ = true; }

  /**
   * @brief 开启结束事件响应
   */
  inline void enable_finish_evt() { evt_disabled_ = false; }

  /**
   * @brief 禁用自动回包
   */
  inline void disable_rsp_msg() { rsp_msg_disabled_ = true; }

  /**
   * @brief 开启自动回包
   */
  inline void enable_rsp_msg() { rsp_msg_disabled_ = false; }

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
  void _notify_finished(cotask::impl::task_impl &task_inst);

 private:
  uint64_t user_id_;
  uint32_t zone_id_;
  uint64_t task_id_;
  result_type ret_code_;
  int32_t rsp_code_;
  bool rsp_msg_disabled_;
  bool evt_disabled_;
  dispatcher_start_data_t start_data_;

  // Additional events
  on_finished_callback_set_t on_finished_callback_;

  rpc::context shared_context_;
};

template <typename TREQ>
class task_action_req_base : public task_action_base {
 public:
  using msg_type = TREQ;

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
}  // namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID
