// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <std/explicit_declare.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include <rpc/rpc_common_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.dtmq.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <cstdint>
#include <memory>
#include <vector>

#include "rpc/dtmq/dtmq_algorithm.h"

namespace atframework {
namespace dtmq {
class DChannelIdKey;
class DChannelMessage;
class DChannelMessageDetail;
class channel_lock_checker;
class channel_subscriber;
class channel_page_info;
}  // namespace dtmq
}  // namespace atframework

namespace rpc {
class context;

namespace dtmq {

/**
 * @brief 获取目标服务器ID
 *
 * @param channel_key 频道Key
 * @param status 状态模式
 * @param replicate_index 副本下标
 * @param mode 选择模式
 * @return uint64_t 服务器ID
 * @note 返回的Readonly副本服务ID可能和Writable副本服务ID相同，此时应该以Writable副本服务ID为准。
 */
DTMQ_PROXY_SDK_API uint64_t
get_target_server_id(const atfw::dtmq::DChannelIdKey& channel_key, replicate_type status, uint64_t replicate_index = 0,
                     logic_hpa_discovery_select_mode mode = logic_hpa_discovery_select_mode::kReady);

/**
 * @brief 获取目标服务器ID
 *
 * @param server_ids 服务器ID列表，第一个是Writable副本，后续是对应index的Readonly副本
 * @param channel_key 频道Key
 * @param replicate_index_count 副本下标数量
 * @param mode 选择模式
 * @return void
 * @note 返回的Readonly副本服务ID可能和Writable副本服务ID相同，此时应该以Writable副本服务ID为准。
 */
DTMQ_PROXY_SDK_API void get_target_server_ids(
    std::vector<uint64_t>& server_ids, const atfw::dtmq::DChannelIdKey& channel_key, uint64_t replicate_index_count = 0,
    logic_hpa_discovery_select_mode mode = logic_hpa_discovery_select_mode::kReady);

/**
 * @brief 检查是否有聊天服务器
 *
 * @return true 有聊天服务器
 * @return false 无聊天服务器
 */
DTMQ_PROXY_SDK_API bool has_dtmq_proxysvr();

/**
 * @brief 发送消息
 *
 * @param ctx RPC上下文
 * @param sender_info 发送者信息
 * @param channel_key 频道键
 * @param detail 消息详情
 * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
 * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
 * @param auto_create_channel 是否自动创建频道
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_message(
    rpc::context& ctx, atfw::dtmq::channel_subscriber&& sender_info, const atfw::dtmq::DChannelIdKey& channel_key,
    atfw::dtmq::DChannelMessageDetail&& detail,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
    std::shared_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr = nullptr,
    bool auto_create_channel = false, bool no_wait = false);

/**
 * @brief 查找消息
 *
 * @param ctx RPC上下文
 * @param channel_key 频道键
 * @param sequence 消息序列号
 * @param msg 消息对象
 * @return rpc::result_code_type 查找结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type find_message(
    rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key, int64_t sequence,
    atfw::dtmq::DChannelMessage& msg);

/**
 * @brief 分页查询消息
 *
 * @param ctx RPC上下文
 * @param channel_key 频道键
 * @param page_info 分页信息
 * @param msgs 消息列表
 * @return rpc::result_code_type 查询结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type page_query_message(
    rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key, atfw::dtmq::channel_page_info& page_info,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs);

}  // namespace dtmq
}  // namespace rpc
