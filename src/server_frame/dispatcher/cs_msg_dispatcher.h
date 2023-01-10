// Copyright 2021 atframework
// Created by owent on 2016/9/27.
//

#ifndef DISPATCHER_CS_MSG_DISPATCHER_H
#define DISPATCHER_CS_MSG_DISPATCHER_H

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/descriptor.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/compiler_features.h>
#include <design_pattern/singleton.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "dispatcher/dispatcher_implement.h"
#include "dispatcher/dispatcher_type_defines.h"

namespace atbus {
namespace protocol {
class msg;
}
}  // namespace atbus

class cs_msg_dispatcher : public dispatcher_implement, public util::design_pattern::singleton<cs_msg_dispatcher> {
 public:
  using msg_op_type_t = dispatcher_implement::msg_op_type_t;
  using msg_raw_t = dispatcher_implement::msg_raw_t;
  using msg_type_t = dispatcher_implement::msg_type_t;

 protected:
  cs_msg_dispatcher();

 public:
  virtual ~cs_msg_dispatcher();
  int32_t init() override;

  const char *name() const override;

  /**
   * @brief 启动关闭命令
   * @note 如果有延时关闭，在本模块关闭返回0前会定期反复调用，直到某次返回值<=0为止
   * @return 0正常关闭，>0需要延时关闭，<0错误码
   */
  int stop() override;

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
   * @brief on create task failed
   * @param start_data start data
   * @param error_code error code
   */
  void on_create_task_failed(dispatcher_start_data_t &start_data, int32_t error_code) override;

  /**
   * deal with cs message data
   * @param msg msg information
   * @param buffer data
   * @param len data length
   * @return 0 or error code
   */
  int32_t dispatch(const atapp::app::message_sender_t &source, const atapp::app::message_t &msg);

  /**
   * send kickoff message to atgateway
   * @param bus_id bus id of atgateway
   * @param session_id session id
   * @param reason kickoff reason
   * @return 0 or error code
   */
  int32_t send_kickoff(uint64_t bus_id, uint64_t session_id, int32_t reason);

  /**
   * send data to client
   * @param bus_id bus id of atgateway
   * @param session_id session id
   * @param buffer data buffer
   * @param len data length
   * @return 0 or error code
   */
  int32_t send_data(uint64_t bus_id, uint64_t session_id, const void *buffer, size_t len);

  /**
   * broadcast data to atgateway
   * @param bus_id bus id of atgateway
   * @param buffer data buffer
   * @param len data length
   * @return 0 or error code
   */
  int32_t broadcast_data(uint64_t bus_id, const void *buffer, size_t len);

  /**
   * broadcast data to multiple clients
   * @param bus_id bus id of atgateway
   * @param session_ids session id
   * @param buffer data buffer
   * @param len data length
   * @return 0 or error code
   */
  int32_t broadcast_data(uint64_t bus_id, const std::vector<uint64_t> &session_ids, const void *buffer, size_t len);

 private:
  bool is_closing_;
  std::unordered_map<msg_type_t, const atframework::DispatcherOptions *> dispatcher_options_map_;
};

#endif  // ATF4G_CO_CS_MSG_DISPATCHER_H
