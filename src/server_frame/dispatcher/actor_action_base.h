// Copyright 2021 atframework
// Created by owent on 2016/11/14.
//

#pragma once

#include <libcopp/utils/features.h>

#include <config/server_frame_build_feature.h>

#include <rpc/rpc_utils.h>

#include <list>
#include <memory>
#include <utility>

#include "dispatcher/dispatcher_type_defines.h"

class actor_action_base {
 public:
  using result_type = int;
  using on_finished_callback_fn_t = std::function<void(const actor_action_base &)>;
  using on_finished_callback_set_t = std::list<on_finished_callback_fn_t>;
  using on_finished_callback_handle_t = on_finished_callback_set_t::iterator;
  enum status_t { EN_AAS_CREATED = 0, EN_AAS_RUNNING, EN_AAS_FINISHED };

 protected:
  actor_action_base();
  explicit actor_action_base(rpc::context *caller_context);
  virtual ~actor_action_base();

 public:
  virtual const char *name() const;

  virtual result_type hook_run();
  virtual result_type operator()() = 0;
  inline uint64_t get_user_id() const { return user_id_; }
  inline uint32_t get_zone_id() const { return zone_id_; }

  virtual int on_success();
  virtual int on_failed();
  virtual int on_complete();

  virtual std::shared_ptr<dispatcher_implement> get_dispatcher() const = 0;
  virtual const char *get_type_name() const = 0;
  virtual const atframework::RpcTraceSpan *get_parent_trace_span() const;

  result_type run(void *priv_data);

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

  /**
   * @brief 返回当前状态
   * @return 当前状态
   */
  inline status_t get_status() const { return status_; }

 protected:
  /**
   * @brief 设置逻辑返回码
   * @note 用于临时存储逻辑操作错误码
   * @param ret_code 返回码
   */
  template <class TArg>
  inline void set_result(TArg &&ret_code) {
    result_ = std::forward<TArg>(ret_code);
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
  inline dispatcher_start_data_t &get_dispatcher_start_data() { return start_data_; }

  /**
   * @brief Get the shared context object
   *
   * @return const rpc::context&
   */
  inline const rpc::context &get_shared_context() const { return shared_context_; }

  /**
   * @brief Get the shared context object
   *
   * @return const rpc::context&
   */
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
  result_type result_;
  int32_t response_code_;
  status_t status_;
  bool response_message_disabled_;
  bool event_disabled_;

  dispatcher_start_data_t start_data_;

  // Additional events
  on_finished_callback_set_t on_finished_callback_;

  rpc::context shared_context_;
};

template <typename TREQ>
class actor_action_req_base : public actor_action_base {
 public:
  using msg_type = TREQ;

 protected:
  inline TREQ &get_request() { return request_msg_; }
  inline const TREQ &get_request() const { return request_msg_; }

 private:
  TREQ request_msg_;
};
