// Copyright 2026 atframework
// Created by owent on 2026-09-20

#pragma once

#include <gsl/select-gsl.h>

#include <transaction_client_handle.h>

#include <config/server_frame_build_feature.h>

#include <utility/protobuf_mini_dumper.h>

#include <cstdint>
#include <string>
#include <utility>

namespace atframework {
namespace friend_api {
class DFriendEvent;
}
}  // namespace atframework

namespace rpc {
namespace friend_api {

struct friend_wal_log_action_getter {
  FRIEND_SDK_MANAGEMENT_API friend_wal_log_action_getter();
  FRIEND_SDK_MANAGEMENT_API friend_wal_log_action_getter(const friend_wal_log_action_getter&);
  FRIEND_SDK_MANAGEMENT_API friend_wal_log_action_getter(friend_wal_log_action_getter&&) noexcept;
  FRIEND_SDK_MANAGEMENT_API ~friend_wal_log_action_getter();
  FRIEND_SDK_MANAGEMENT_API friend_wal_log_action_getter& operator=(const friend_wal_log_action_getter&);
  FRIEND_SDK_MANAGEMENT_API friend_wal_log_action_getter& operator=(friend_wal_log_action_getter&&) noexcept;

  FRIEND_SDK_MANAGEMENT_API int32_t operator()(const atfw::friend_api::DFriendEvent& event) const noexcept;
};

/**
 * @brief 获取好友事件日志的哈希码
 *
 * @param evt 好友事件日志
 * @return uint64_t 哈希码
 */
FRIEND_SDK_MANAGEMENT_API uint64_t get_hash_code(const atfw::friend_api::DFriendEvent& evt) noexcept;

/**
 * @brief 设置好友事件日志的哈希码
 *
 * @param evt 好友事件日志
 * @param hash_code 哈希码
 */
FRIEND_SDK_MANAGEMENT_API void set_hash_code(atfw::friend_api::DFriendEvent& evt, uint64_t hash_code) noexcept;

/**
 * @brief 计算好友事件日志的哈希码
 *
 * @param previous 之前的哈希码
 * @param evt 好友事件日志
 * @return uint64_t 新的哈希码
 */
FRIEND_SDK_MANAGEMENT_API uint64_t calculate_hash_code(uint64_t previous,
                                                       const atfw::friend_api::DFriendEvent& evt) noexcept;

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
  using meta_result_type = typename wal_object::meta_result_type;

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

  target.get_log_key = [](const wal_object&, const log_type& log) ->
      typename wal_object::log_key_type { return log.event_id(); };

  target.get_meta = [](const wal_object&, const log_type& log) -> meta_result_type {
    return meta_result_type::make_success(protobuf_to_system_clock(log.create_timepoint()), log.event_id(),
                                          log.event_case());
  };

  target.set_meta = [](const wal_object&, log_type& log, const typename wal_object::meta_type& meta) {
    // log.event_case = meta.action_case; // event_case will be created by mutable_*
    protobuf_from_system_clock(*log.mutable_create_timepoint(), meta.timepoint);
    log.set_event_id(meta.log_key);
  };
}

using atfw::distributed_system::transaction_client_handle;

FRIEND_SDK_MANAGEMENT_API std::string friend_key_to_transaction_participator_key(uint32_t zone_id, uint64_t user_id);
FRIEND_SDK_MANAGEMENT_API std::pair<uint32_t, uint64_t> transaction_participator_key_to_friend_key(
    gsl::string_view key);

FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::vtable_type& get_default_transaction_delegator();
FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::transaction_options& get_normal_transaction_options();
FRIEND_SDK_MANAGEMENT_API const transaction_client_handle::transaction_options& get_force_commit_transaction_options();

}  // namespace friend_api
}  // namespace rpc
