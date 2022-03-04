// Copyright 2021 atframework
// Created by owent on 2016/9/27.
//

#ifndef DISPATCHER_SS_MSG_DISPATCHER_H
#define DISPATCHER_SS_MSG_DISPATCHER_H

#pragma once

#include <config/compiler_features.h>
#include <design_pattern/singleton.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/descriptor.h>

#include <config/compiler/protobuf_suffix.h>

#include <gsl/select-gsl.h>

#include <string>

#include "dispatcher/dispatcher_implement.h"
#include "dispatcher/dispatcher_type_defines.h"

namespace atbus {
namespace protocol {
class msg;
}
}  // namespace atbus

namespace atframework {
class SSMsg;
}

class ss_msg_dispatcher : public dispatcher_implement, public util::design_pattern::singleton<ss_msg_dispatcher> {
 public:
  using msg_op_type_t = dispatcher_implement::msg_op_type_t;
  using msg_raw_t = dispatcher_implement::msg_raw_t;
  using msg_type_t = dispatcher_implement::msg_type_t;

 protected:
  ss_msg_dispatcher();

 public:
  virtual ~ss_msg_dispatcher();

  int32_t init() override;

  /**
   * @brief 获取任务信息
   * @param raw_msg 消息抽象结构
   * @return 相关的任务id
   */
  uint64_t pick_msg_task_id(msg_raw_t &raw_msg) override;

  /**
   * @brief 获取消息名称
   * @param raw_msg 消息抽象结构
   * @return 消息类型ID
   */
  msg_type_t pick_msg_type_id(msg_raw_t &raw_msg) override;

  /**
   * @brief 获取消息的RPC名字
   * @param raw_msg 消息抽象结构
   * @return 消息的RPC名字,如果不是RPC消息，返回空字符串
   */
  const std::string &pick_rpc_name(msg_raw_t &raw_msg) override;

  /**
   * @brief 获取操作类型
   * @param raw_msg 消息抽象结构
   * @note 这只是一个调度曾规范，不强制执行。详情 @see PROJECT_NAMESPACE_ID::EnMsgOpType
   * @return 消息操作类型
   */
  msg_op_type_t pick_msg_op_type(msg_raw_t &raw_msg) override;

  /**
   * @brief 根据类型ID获取action或actor选项
   * @param raw_msg 消息抽象结构
   * @return 返回action或actor选项或NULL
   */
  const atframework::DispatcherOptions *get_options_by_message_type(msg_type_t msg_type) override;

  /**
   * deal with cs message data
   * @param source data source wrapper
   * @param msg msg wrapper
   * @return 0 or error code
   */
  int32_t dispatch(const atapp::app::message_sender_t &source, const atapp::app::message_t &msg);

  /**
   * notify send failed
   * @param source data source wrapper
   * @param msg msg wrapper
   * @param error_code error code
   * @return 0 or error code
   */
  int32_t on_receive_send_data_response(const atapp::app::message_sender_t &source, const atapp::app::message_t &msg,
                                        int32_t error_code);

  /**
   * @brief on create task failed
   * @param start_data start data
   * @param error_code error code
   */
  void on_create_task_failed(start_data_t &start_data, int32_t error_code) override;

  /**
   * allocate a message sequence
   * @return allocated sequence
   */
  uint64_t allocate_sequence();

 public:
  int32_t send_to_proc(uint64_t bus_id, atframework::SSMsg &ss_msg, bool ignore_discovery = false);
  int32_t send_to_proc(uint64_t bus_id, const void *msg_buf, size_t msg_len, bool ignore_discovery);
  bool is_target_server_available(uint64_t bus_id) const;
  bool is_target_server_available(gsl::string_view node_name) const;

 private:
  uint64_t sequence_allocator_;
  const google::protobuf::OneofDescriptor *get_body_oneof_desc() const;
};

#endif  // ATF4G_CO_SS_MSG_DISPATCHER_H
