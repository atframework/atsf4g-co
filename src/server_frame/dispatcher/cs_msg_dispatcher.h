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

#include <gsl/select-gsl.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dispatcher/dispatcher_implement.h"
#include "dispatcher/dispatcher_type_defines.h"

class cs_msg_dispatcher : public dispatcher_implement {
 public:
  using msg_raw_t = dispatcher_implement::msg_raw_t;
  using msg_type_t = dispatcher_implement::msg_type_t;

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(cs_msg_dispatcher)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(cs_msg_dispatcher)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(cs_msg_dispatcher)
#endif

 private:
  SERVER_FRAME_API cs_msg_dispatcher();

 public:
  SERVER_FRAME_API virtual ~cs_msg_dispatcher();

  SERVER_FRAME_API int32_t init() override;

  SERVER_FRAME_API const char *name() const override;

  /**
   * @brief 启动关闭命令
   * @note 如果有延时关闭，在本模块关闭返回0前会定期反复调用，直到某次返回值<=0为止
   * @return 0正常关闭，>0需要延时关闭，<0错误码
   */
  SERVER_FRAME_API int stop() override;

  /**
   * @brief 获取任务信息
   * @param raw_msg 消息抽象结构
   * @return 相关的任务id
   */
  SERVER_FRAME_API uint64_t pick_msg_task_id(msg_raw_t &raw_msg) override;

  /**
   * @brief 获取消息的RPC名字
   * @param raw_msg 消息抽象结构
   * @return 消息的RPC名字,如果不是RPC消息，返回空字符串
   */
  SERVER_FRAME_API const std::string &pick_rpc_name(msg_raw_t &raw_msg) override;

  /**
   * @brief on create task failed
   * @param start_data start data
   * @param error_code error code
   */
  SERVER_FRAME_API void on_create_task_failed(dispatcher_start_data_type &start_data, int32_t error_code) override;

  /**
   * deal with cs message data
   * @param msg msg information
   * @param buffer data
   * @param len data length
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t dispatch(const atfw::atapp::app::message_sender_t &source,
                                    const atfw::atapp::app::message_t &msg);

  /**
   * send kickoff message to atgateway
   * @param node_id bus id of atgateway
   * @param session_id session id
   * @param reason kickoff reason
   * @param message kickoff message
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t send_kickoff(uint64_t node_id, uint64_t session_id, int32_t reason,
                                        atfw::util::nostd::string_view message);

  /**
   * send set router message to atgateway
   * @param node_id bus id of atgateway
   * @param session_id session id
   * @param target_service_id target service id
   * @param target_service_name target service name
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t send_set_router(uint64_t node_id, uint64_t session_id, uint64_t target_service_id,
                                           atfw::util::nostd::string_view target_service_name);

  /**
   * send data to client
   * @param node_id bus id of atgateway
   * @param session_id session id
   * @param buffer data buffer
   * @param len data length
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t send_data(uint64_t node_id, uint64_t session_id, const void *buffer, size_t len);

  /**
   * broadcast data to atgateway
   * @param node_id bus id of atgateway
   * @param buffer data buffer
   * @param len data length
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t broadcast_data(uint64_t node_id, const void *buffer, size_t len);

  /**
   * broadcast data to multiple clients
   * @param node_id bus id of atgateway
   * @param session_ids session id
   * @param buffer data buffer
   * @param len data length
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t broadcast_data(uint64_t node_id, const std::vector<uint64_t> &session_ids,
                                          const void *buffer, size_t len);

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  // Synchronous unit-test seam at the unified gateway-send boundary. All downstream operations
  // (send_data, send_kickoff, send_set_router and both broadcast_data overloads) serialize their
  // atfw::gateway::server_message and then pass through this hook before the bus node is touched.
  // When the hook reports the send handled, no bus IO happens. Only available in builds with
  // PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS.
  struct ATFW_UTIL_SYMBOL_VISIBLE unit_test_gateway_send_request {
    uint64_t node_id = 0;
    int32_t service_type = 0;
    uint64_t session_id = 0;
    // Multi-session broadcast only: the requested session id list for observability. The production
    // fallback intentionally keeps ignoring it, matching the pre-seam behavior.
    const std::vector<uint64_t> *session_ids = nullptr;
    // Serialized atfw::gateway::server_message exactly as it would go onto the bus.
    gsl::span<const unsigned char> data;
  };

  // Returns true when the send is handled; result_code then carries the function-level result.
  using unit_test_gateway_send_hook_t =
      std::function<bool(const unit_test_gateway_send_request &, int32_t &result_code)>;

  SERVER_FRAME_API static void set_gateway_send_hook_for_unit_test(unit_test_gateway_send_hook_t hook);
  SERVER_FRAME_API static const unit_test_gateway_send_hook_t &get_gateway_send_hook_for_unit_test() noexcept;
#endif

 private:
  // Unified gateway-send boundary: every downstream operation serializes its server_message and
  // funnels through this function, which evaluates the unit-test hook before touching the bus node.
  int32_t send_serialized_to_gateway(uint64_t node_id, int32_t service_type, uint64_t session_id,
                                     const std::vector<uint64_t> *session_ids, const std::string &packed_buffer);

 private:
  bool is_closing_;
  std::unordered_map<msg_type_t, const atframework::DispatcherOptions *> dispatcher_options_map_;
};

#endif  // ATF4G_CO_CS_MSG_DISPATCHER_H
