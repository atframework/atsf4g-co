// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/match_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <data/user_key_hash_helper.h>
#include <distributed_system/wal_publisher.h>
#include <memory/object_allocator.h>

#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

class matching_room;

namespace rpc {
class context;
}

// 匹配 WAL 不承担房间持久化；房间状态由快照提供，WAL 只保留可重放的通知日志。
struct matching_wal_storage {};

// WAL 回调同时需要 RPC 上下文发送通知，并把发送错误返回给调用链。
struct matching_wal_context {
  std::reference_wrapper<rpc::context> context;
  std::reference_wrapper<int32_t> result_code;

  matching_wal_context(rpc::context& ctx, int32_t& output_result);
};

// oneof case 是 WAL 的 action key，用于框架分组和合并动作。
struct matching_wal_action_getter {
  PROJECT_NAMESPACE_ID::DMatchingEventLog::EventCase operator()(
      const PROJECT_NAMESPACE_ID::DMatchingEventLog& log) const noexcept {
    return log.event_case();
  }
};

struct matching_wal_action_hash {
  size_t operator()(PROJECT_NAMESPACE_ID::DMatchingEventLog::EventCase value) const noexcept {
    return std::hash<int32_t>{}(static_cast<int32_t>(value));
  }
};

struct matching_wal_action_equal {
  bool operator()(PROJECT_NAMESPACE_ID::DMatchingEventLog::EventCase left,
                  PROJECT_NAMESPACE_ID::DMatchingEventLog::EventCase right) const noexcept {
    return left == right;
  }
};

struct matching_wal_log_operator : public atfw::util::distributed_system::wal_log_operator<
                                       int64_t, PROJECT_NAMESPACE_ID::DMatchingEventLog, matching_wal_action_getter,
                                       std::less<>, matching_wal_action_hash, matching_wal_action_equal,
                                       atfw::memory::stl::allocator<PROJECT_NAMESPACE_ID::DMatchingEventLog>> {};

using matching_wal_subscriber_private_data =
    atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::DMatchingSubscriberData>;

// 每个玩家是一个订阅者；同一 lobbysvr 的玩家会合并成一次通知 RPC。
struct matching_wal_subscriber
    : public atfw::util::distributed_system::wal_subscriber<
          matching_wal_subscriber_private_data, PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> {};

using matching_wal_publisher =
    atfw::util::distributed_system::wal_publisher<matching_wal_storage, matching_wal_log_operator, matching_wal_context,
                                                  matching_room*, matching_wal_subscriber>;

using matching_wal_result_code = atfw::util::distributed_system::wal_result_code;
using matching_wal_subscriber_group_key = std::pair<uint64_t, uint64_t>;
using matching_wal_subscriber_group =
    std::map<matching_wal_subscriber_group_key, std::vector<matching_wal_publisher::subscriber_pointer>>;
using matching_wal_object = matching_wal_publisher::object_type;

namespace matching_wal_detail {

// 按 lobbysvr 和 Unit 聚合仍在线且路由有效的 WAL 订阅者。
matching_wal_subscriber_group collect_subscribers(matching_wal_publisher::subscriber_iterator begin,
                                                  matching_wal_publisher::subscriber_iterator end);

// 向订阅者分组发送快照或增量日志，并仅在发送成功后推进发送游标。
void send_to_subscribers(matching_wal_publisher& publisher, const matching_wal_subscriber_group& groups,
                         const PROJECT_NAMESPACE_ID::SSMatchingEventSync& message, matching_wal_context param,
                         int64_t last_event_id);

// 创建并缓存匹配 WAL publisher 的回调表。
matching_wal_publisher::vtable_pointer create_vtable();

// 创建匹配 WAL publisher 的日志保留和订阅超时配置。
matching_wal_publisher::configure_pointer create_configure();

}  // namespace matching_wal_detail

// 创建绑定指定匹配房间的 WAL publisher。
matching_wal_log_operator::strong_ptr<matching_wal_publisher> create_matching_wal_publisher(matching_room& room);
