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
#include <mem_pool/lru_map.h>

#include <string>

#include "dispatcher/dispatcher_implement.h"
#include "dispatcher/dispatcher_type_defines.h"

namespace atframework {
namespace atbus {
class message;
}  // namespace atbus
}  // namespace atframework

namespace atframework {
namespace atapp {
namespace protocol {
class atapp_metadata;
}  // namespace protocol
}  // namespace atapp
}  // namespace atframework

namespace atframework {
class SSMsg;
}

struct ATFW_UTIL_SYMBOL_VISIBLE ss_msg_logic_index {
  uint64_t type_id;
  gsl::string_view type_name;
  uint64_t zone_id;

  inline ss_msg_logic_index() noexcept : type_id(0), type_name(), zone_id(0) {}
  inline ss_msg_logic_index(uint64_t input_type_id, uint64_t input_zone_id)
      : type_id(input_type_id), type_name(), zone_id(input_zone_id) {}
  inline ss_msg_logic_index(gsl::string_view input_type_name, uint64_t input_zone_id) noexcept
      : type_id(0), type_name(input_type_name), zone_id(input_zone_id) {}

  inline ss_msg_logic_index(const ss_msg_logic_index &) noexcept = default;
  inline ss_msg_logic_index(ss_msg_logic_index &&) noexcept = default;
  inline ss_msg_logic_index &operator=(const ss_msg_logic_index &) noexcept = default;
  inline ss_msg_logic_index &operator=(ss_msg_logic_index &&) noexcept = default;
};

class ss_msg_dispatcher : public dispatcher_implement {
 public:
  using msg_raw_t = dispatcher_implement::msg_raw_t;
  using msg_type_t = dispatcher_implement::msg_type_t;

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(ss_msg_dispatcher)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(ss_msg_dispatcher)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(ss_msg_dispatcher)
#endif

 private:
  SERVER_FRAME_API ss_msg_dispatcher();

 public:
  SERVER_FRAME_API virtual ~ss_msg_dispatcher();

  SERVER_FRAME_API int32_t init() override;

  SERVER_FRAME_API const char *name() const override;

  SERVER_FRAME_API int stop() override;

  SERVER_FRAME_API int tick() override;

  SERVER_FRAME_API void ready() override final;

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

  SERVER_FRAME_API const std::string &pick_rpc_name(const atframework::SSMsg &ss_msg);

  /**
   * deal with cs message data
   * @param source data source wrapper
   * @param msg msg wrapper
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t dispatch(const atfw::atapp::app::message_sender_t &source,
                                    const atfw::atapp::app::message_t &msg);

  /**
   * notify send failed
   * @param source data source wrapper
   * @param msg msg wrapper
   * @param error_code error code
   * @return 0 or error code
   */
  SERVER_FRAME_API int32_t on_receive_send_data_response(const atfw::atapp::app::message_sender_t &source,
                                                         const atfw::atapp::app::message_t &msg, int32_t error_code);

  /**
   * @brief on create task failed
   * @param start_data start data
   * @param error_code error code
   */
  SERVER_FRAME_API void on_create_task_failed(dispatcher_start_data_type &start_data, int32_t error_code) override;

  /**
   * allocate a message sequence
   * @return allocated sequence
   */
  SERVER_FRAME_API uint64_t allocate_sequence();

 public:
  SERVER_FRAME_API int32_t send_to_proc(uint64_t node_id, atframework::SSMsg &ss_msg, bool ignore_discovery = false);
  SERVER_FRAME_API int32_t send_to_proc(uint64_t node_id, const void *msg_buf, size_t msg_len, uint64_t sequence,
                                        bool ignore_discovery);
  SERVER_FRAME_API int32_t send_to_proc(const std::string &node_name, atframework::SSMsg &ss_msg,
                                        bool ignore_discovery = false);
  SERVER_FRAME_API int32_t send_to_proc(const atfw::atapp::etcd_discovery_node &node, atframework::SSMsg &ss_msg,
                                        bool ignore_discovery = false);
  SERVER_FRAME_API int32_t send_to_proc(const atfw::atapp::etcd_discovery_node &node, const void *msg_buf,
                                        size_t msg_len, uint64_t sequence, bool ignore_discovery = false);

  SERVER_FRAME_API bool is_target_server_available(uint64_t node_id) const;
  SERVER_FRAME_API bool is_target_server_available(const std::string &node_name) const;

  /**
   * @brief 广播消息
   * @param ss_msg 要发送的消息
   * @param index 逻辑索引
   * @param metadata metadata匹配，传空为跳过这个筛选规则
   * @return 所有成功返回0，否则返回第一个错误码
   */
  SERVER_FRAME_API int32_t broadcast(atframework::SSMsg &ss_msg, const ss_msg_logic_index &index,
                                     ::atfw::atapp::protocol::atapp_metadata *metadata = nullptr);

 private:
  void setup_metrics();

  static void dns_lookup_callback(uv_getaddrinfo_t *req, int status, struct addrinfo *res) noexcept;

 public:
  SERVER_FRAME_API void *get_dns_lookup_rpc_type() noexcept;
  SERVER_FRAME_API int32_t send_dns_lookup(gsl::string_view domain, uint64_t sequence, uint64_t task_id);

 private:
  uint64_t sequence_allocator_;

  struct dns_lookup_async_data {
    uint64_t task_id;
    void *rpc_type_address;
    uint64_t rpc_sequence;
    std::string domain;
    uv_getaddrinfo_t request;
    time_t start_timepoint;
    time_t timeout_timepoint;
  };

  atfw::util::mempool::lru_map<uint64_t, dns_lookup_async_data> running_dns_lookup_;
};

#endif  // ATF4G_CO_SS_MSG_DISPATCHER_H
