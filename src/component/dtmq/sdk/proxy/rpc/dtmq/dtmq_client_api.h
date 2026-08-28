// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <std/explicit_declare.h>

#include <memory/rc_ptr.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include <rpc/rpc_common_types.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "rpc/dtmq/dtmq_algorithm.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/repeated_field.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

namespace atframework {
namespace dtmq {
class DChannelIdKey;
class DChannelMessage;
class DChannelMessageDetail;
class SSChannelDestroyChannelReq;
class SSChannelResetLockReq;
class SSChannelResetLockRsp;
class SSChannelUpdateReq;
class SSChannelUpdateRsp;
class channel_lock_checker;
class channel_subscriber;
class channel_page_info;
}  // namespace dtmq
}  // namespace atframework

namespace google {
namespace protobuf {
class Empty;
}  // namespace protobuf
}  // namespace google

namespace rpc {
class context;

namespace dtmq {

DTMQ_PROXY_SDK_API uint64_t normalize_replicate_index(uint64_t replicate_index, uint32_t readonly_replicate_count);

DTMQ_PROXY_SDK_API uint64_t normalize_replicate_index(uint64_t replicate_index,
                                                      const atfw::dtmq::DChannelIdKey& channel_key);

/**
 * @brief 获取目标服务器ID
 *
 * @param channel_key 频道Key
 * @param rep_type 副本类型
 * @param replicate_index 副本下标
 * @param mode 选择模式
 * @return uint64_t 服务器ID
 * @note 返回的Readonly副本服务ID可能和Writable副本服务ID相同，此时应该以Writable副本服务ID为准。
 */
DTMQ_PROXY_SDK_API uint64_t get_target_server_id(
    const atfw::dtmq::DChannelIdKey& channel_key, replicate_type rep_type, uint64_t replicate_index = 0,
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
    atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
    atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr = nullptr,
    bool auto_create_channel = false, bool no_wait = false);

/**
 * @brief 批量发送消息(一次请求按顺序追加多条消息)
 *
 * @param ctx RPC上下文
 * @param sender_info 发送者信息
 * @param channel_key 频道键
 * @param details 消息详情列表(不能为空)
 * @param compare_and_maybe_reset_lock_ptr 锁检查器指针
 * @param compare_and_maybe_reset_lock_rsp_ptr 锁检查器响应指针
 * @param auto_create_channel 是否自动创建频道
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type send_message(
    rpc::context& ctx, atfw::dtmq::channel_subscriber&& sender_info, const atfw::dtmq::DChannelIdKey& channel_key,
    google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessageDetail>&& details,
    atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_ptr = nullptr,
    atfw::util::memory::strong_rc_ptr<atfw::dtmq::channel_lock_checker> compare_and_maybe_reset_lock_rsp_ptr = nullptr,
    bool auto_create_channel = false, bool no_wait = false);

/**
 * @brief 更新频道数据(Writable副本)，目标服务器选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(目标服务器从其 channel_key 字段提取)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type update(
    rpc::context& ctx, atfw::dtmq::SSChannelUpdateReq& req, atfw::dtmq::SSChannelUpdateRsp& rsp, bool no_wait = false);

/**
 * @brief 重置频道乐观锁(Writable副本)，目标服务器选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(目标服务器从其 channel_key 字段提取)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type reset_lock(
    rpc::context& ctx, atfw::dtmq::SSChannelResetLockReq& req, atfw::dtmq::SSChannelResetLockRsp& rsp,
    bool no_wait = false);

/**
 * @brief 销毁频道(Writable副本)，目标服务器选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(目标服务器从其 channel_key 字段提取)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR DTMQ_PROXY_SDK_API rpc::result_code_type destroy_channel(
    rpc::context& ctx, atfw::dtmq::SSChannelDestroyChannelReq& req, google::protobuf::Empty& rsp,
    bool no_wait = false);

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
    rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key, uint64_t replicate_index, int64_t sequence,
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
    rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key, uint64_t replicate_index,
    atfw::dtmq::channel_page_info& page_info, google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessage>& msgs);

}  // namespace dtmq
}  // namespace rpc
