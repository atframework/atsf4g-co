// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include <rpc/rpc_common_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstdint>
#include <memory>
#include <string>

#include "config/compile_optimize.h"
#include "memory/rc_ptr.h"
#include "rpc/dtmq/dtmq_algorithm.h"
#include "rpc/dtmq/dtmq_client_api.h"

namespace rpc {
class context;

namespace dtmq {
class client_subscriber {
 public:
  using ptr_t = std::shared_ptr<client_subscriber>;

  struct ATFW_UTIL_SYMBOL_VISIBLE subscriber_options {
    DTMQ_PROXY_SDK_API subscriber_options(std::string input_subscriber_key);
    DTMQ_PROXY_SDK_API ~subscriber_options();

    DTMQ_PROXY_SDK_API subscriber_options(const subscriber_options& other) = default;
    DTMQ_PROXY_SDK_API subscriber_options(subscriber_options&& other) = default;
    DTMQ_PROXY_SDK_API subscriber_options& operator=(const subscriber_options& other) = default;
    DTMQ_PROXY_SDK_API subscriber_options& operator=(subscriber_options&& other) = default;

    std::string subscriber_key;
  };

 private:
  struct ctor_guard;

 public:
  client_subscriber(const client_subscriber&) = delete;
  client_subscriber(client_subscriber&&) = delete;
  client_subscriber& operator=(const client_subscriber&) = delete;
  client_subscriber& operator=(client_subscriber&&) = delete;

  explicit client_subscriber(ctor_guard&);

  DTMQ_PROXY_SDK_API ~client_subscriber();

  /**
   * @brief 创建频道订阅者，如果参数错误返回空指针
   *
   * @param channel_key 频道Key
   * @param options 订阅者信息
   * @return DTMQ_PROXY_SDK_API
   */
  static DTMQ_PROXY_SDK_API atfw::util::nostd::nullable<ptr_t> create(const atfw::dtmq::DChannelIdKey& channel_key,
                                                                      const subscriber_options& options);

  /**
   * @brief 使用订阅者的服务必须接入处理接收到的频道事件
   *
   * @param ctx RPC上下文
   * @param event_sync 收到的实践同步消息
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API static void global_receive_channel_event(
      rpc::context& ctx, const atfw::dtmq::SSChannelEventSync event_sync);

  /**
   * @brief 使用订阅者的服务必须接入定时器Tick调用
   *
   * @param ctx RPC上下文
   * @return 0表示无任何定时器事件触发，< 0表示错误码，> 0表示触发的定时器事件数量
   */
  DTMQ_PROXY_SDK_API static int32_t global_tick(rpc::context& ctx);

  /**
   * @brief 发送消息
   *
   * @param ctx RPC上下文
   * @param detail 消息详情
   * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
   * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
   * @param auto_create_channel 是否自动创建频道
   * @param no_wait 是否不等待
   * @return rpc::result_code_type 发送结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_message(
      rpc::context& ctx, atfw::dtmq::DChannelMessageDetail&& detail,
      std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
      std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr = nullptr,
      bool auto_create_channel = false, bool no_wait = false);

  /**
   * @brief 查找消息
   *
   * @param ctx RPC上下文
   * @param sequence 消息序列号
   * @param msg 消息对象
   * @return rpc::result_code_type 查找结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type find_message(rpc::context& ctx,
                                                                                     int64_t sequence,
                                                                                     atfw::dtmq::DChannelMessage& msg);

  /**
   * @brief 分页查询消息
   *
   * @param ctx RPC上下文
   * @param page_info 分页信息
   * @param msgs 消息列表
   * @return rpc::result_code_type 查询结果
   */
  ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type page_query_message(
      rpc::context& ctx, atfw::dtmq::channel_page_info& page_info,
      google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs);

 private:
  struct subscriber_internal_data;
  atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<subscriber_internal_data>> internal_data_;
};

}  // namespace dtmq
}  // namespace rpc
