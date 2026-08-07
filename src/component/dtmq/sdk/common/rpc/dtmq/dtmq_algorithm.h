// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

#include <distributed_system/wal_common_defs.h>
#include <log/log_wrapper.h>

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace atframework {
namespace dtmq {
class DChannelIdKey;
class DChannelMessage;
}  // namespace dtmq
}  // namespace atframework

namespace rpc {
namespace dtmq {

enum class replicate_type : uint32_t {
  kWritable = 0,
  kReadonly = 1,
};

/**
 * @brief 订阅哈希不匹配事件的结构体
 *
 * @tparam LogKeyT 日志键类型
 */
template <class LogKeyT>
struct ATFW_UTIL_SYMBOL_VISIBLE hash_mismatch_subscribe {
  template <class InputLogKeyT>
  ATFW_UTIL_FORCEINLINE explicit hash_mismatch_subscribe(std::string in_channel_id, InputLogKeyT&& in_log_key,
                                                         std::chrono::system_clock::time_point time)
      : channel_id(std::move(in_channel_id)),
        times(1),
        log_key(std::forward<InputLogKeyT>(in_log_key)),
        next_need_snapshot_timestamp(time) {}

  ATFW_UTIL_FORCEINLINE explicit hash_mismatch_subscribe()
      : times(1), log_key{}, next_need_snapshot_timestamp(std::chrono::system_clock::now()) {}

  std::string channel_id;
  int32_t times;
  LogKeyT log_key;
  std::chrono::system_clock::time_point next_need_snapshot_timestamp;
};

/**
 * @brief 收集无效事件
 *
 * @tparam Channel 频道类型
 * @param channel 频道对象
 */
template <class Channel>
ATFW_UTIL_SYMBOL_VISIBLE void collect_invalid_event(gsl::string_view channel_id, Channel& channel) {
  if (channel.get_log_manager().get_all_logs().empty()) {
    FWLOGERROR("channel {} log is empty!", channel_id);
    return;
  }

  std::stringstream err_msg;
  int32_t count = 0;
  for (auto iter = channel.get_log_manager().get_all_logs().rbegin();
       iter != channel.get_log_manager().get_all_logs().rend(); ++iter) {
    if (count >= 5) {
      break;
    }
    count++;
    err_msg << "sequence: " << (*iter)->sequence() << ", hash: " << (*iter)->hash_code() << "; ";
  }

  FWLOGERROR("channel {}: {}", channel_id, err_msg.str());
}

/**
 * @brief 获取聊天日志的哈希码
 *
 * @param channel_log 聊天日志
 * @return uint64_t 哈希码
 */
DTMQ_COMMON_SDK_API uint64_t get_hash_code(const atfw::dtmq::DChannelMessage& channel_log) noexcept;

/**
 * @brief 设置聊天日志的哈希码
 *
 * @param channel_log 聊天日志
 * @param hash_code 哈希码
 */
DTMQ_COMMON_SDK_API void set_hash_code(atfw::dtmq::DChannelMessage& channel_log, uint64_t hash_code) noexcept;

/**
 * @brief 计算哈希码
 *
 * @param previous 之前的哈希码
 * @param channel_log 聊天日志
 * @return uint64_t 新的哈希码
 */
DTMQ_COMMON_SDK_API uint64_t calculate_hash_code(uint64_t previous,
                                                 const atfw::dtmq::DChannelMessage& channel_log) noexcept;

/**
 * @brief 设置通用虚表
 *
 * @tparam WalObjectType 日志对象类型
 * @param target 目标虚表
 */
template <class WalObjectType>
ATFW_UTIL_SYMBOL_VISIBLE inline void setup_common_vtable(typename WalObjectType::vtable_type& target) {
  using wal_object = WalObjectType;
  using log_type = typename wal_object::log_type;
  using hash_code_type = typename wal_object::hash_code_type;

  target.get_hash_code = [](const wal_object&, const log_type& channel_log) -> hash_code_type {
    return get_hash_code(channel_log);
  };
  target.set_hash_code = [](const wal_object&, log_type& channel_log, hash_code_type value) {
    set_hash_code(channel_log, value);
  };
  target.calculate_hash_code = [](const wal_object&, hash_code_type previous,
                                  const log_type& channel_log) -> hash_code_type {
    return calculate_hash_code(previous, channel_log);
  };
}

/**
 * @brief 创建单播频道ID的标准算法
 *
 * @param type_id 类型ID
 * @param zone_id 区域ID
 * @param instance_id 实例ID
 * @return std::string 频道ID
 */
DTMQ_COMMON_SDK_API std::string make_unicast_channel_id(uint32_t type_id, uint64_t zone_id, uint64_t instance_id);

/**
 * @brief 解析单播频道的频道类型
 *
 * @param channel_id 频道ID
 * @return 解析的频道类型
 */
DTMQ_COMMON_SDK_API uint32_t parse_unicast_channel_type_from_channel_id(gsl::string_view channel_id);

/**
 * @brief 创建区域广播频道ID的标准算法
 *
 * @param type_id 类型ID
 * @param zone_id 区域ID
 * @return std::string 频道ID
 */
DTMQ_COMMON_SDK_API std::string make_zone_broadcast_channel_id(uint32_t type_id, uint64_t zone_id);

/**
 * @brief 解析区域广播频道的频道类型
 *
 * @param channel_id 频道ID
 * @return 解析的频道类型
 */
DTMQ_COMMON_SDK_API uint32_t parse_zone_broadcast_channel_type_from_channel_id(gsl::string_view channel_id);

/**
 * @brief 创建世界广播频道ID的标准算法
 *
 * @param type_id 类型ID
 * @param world_id 世界ID
 * @return std::string 频道ID
 */
DTMQ_COMMON_SDK_API std::string make_world_broadcast_channel_id(uint32_t type_id, uint64_t world_id);

/**
 * @brief 解析世界广播频道的频道类型
 *
 * @param channel_id 频道ID
 * @return 解析的频道类型
 */
DTMQ_COMMON_SDK_API uint32_t parse_world_broadcast_channel_type_from_channel_id(gsl::string_view channel_id);

/**
 * @brief 创建世界分区频道ID的标准算法
 *
 * @param type_id 类型ID
 * @param world_id 世界ID
 * @param partition_id 分区ID
 * @return std::string 频道ID
 */
DTMQ_COMMON_SDK_API std::string make_world_partition_channel_id(uint32_t type_id, uint64_t world_id,
                                                                uint64_t partition_id);

/**
 * @brief 解析世界分区频道的频道类型
 *
 * @param channel_id 频道ID
 * @return 解析的频道类型
 */
DTMQ_COMMON_SDK_API uint32_t parse_world_partition_channel_type_from_channel_id(gsl::string_view channel_id);

}  // namespace dtmq
}  // namespace rpc
