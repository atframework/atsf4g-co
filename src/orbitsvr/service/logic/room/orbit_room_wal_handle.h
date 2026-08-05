// Copyright 2026 atframework
// Created by atsf4g-co battle module migration

// 战斗房间 WAL 发布搭框（orbit_room_wal_handle）
//
// 说明：本文件为「接入 WAL 模块」的搭框版本——
//   - 基于 atframe_utils 的 util::distributed_system::wal_publisher 定义发布者类型；
//   - 只留出业务调用接口（alloc_event_id / add_event_log / broadcast_events / dump /
//     subscribe / unsubscribe / update_acknowledge），接口内部逻辑为空（TODO 占位）；
//   - 后续 WAL 将被 DTMQ 模块替换（接入流程另行提供），业务代码只依赖本文件暴露的接口，
//     届时以相同接口替换实现即可，业务侧无需改动。

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <distributed_system/wal_publisher.h>

#include <rpc/rpc_common_types.h>

#include <config/extern_service_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.pb.h>
#include <protocol/pbdesc/com.struct.orbit.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <data/player_key_hash_helper.h>

namespace rpc {
class context;
}  // namespace rpc

class orbit_room;

// ====== WAL 发布者类型定义 ======

// 存储：无状态（本版不落盘，后续 DTMQ 替换时由 DTMQ 承载）
struct orbit_room_storage_type {};

// WAL 发布上下文：可能发起异步任务
struct orbit_room_wal_publisher_context {
  std::shared_ptr<rpc::context> context;  // 可能要发起异步任务
  std::reference_wrapper<int32_t> result_code;

  explicit orbit_room_wal_publisher_context(rpc::context& ctx, int32_t& output_result);
};

// 事件类型 getter：取 DOrbitRoomEventLog 的 oneof event case
struct orbit_room_wal_publisher_log_action_getter {
  inline PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::EventCase operator()(
      const PROJECT_NAMESPACE_ID::DOrbitRoomEventLog& log) noexcept {
    return log.event_case();
  }
};

struct orbit_room_log_action_hash_t {
  inline size_t operator()(const PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::EventCase& key) const noexcept {
    return std::hash<int>()(key);
  }
};

struct orbit_room_log_action_equal_t {
  inline bool operator()(const PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::EventCase& l,
                         const PROJECT_NAMESPACE_ID::DOrbitRoomEventLog::EventCase& r) const noexcept {
    return l == r;
  }
};

// 订阅者私有数据：记录订阅者最后确认的事件 id（对账用，本版占位）
struct orbit_room_wal_subscriber_private_data {
  int64_t last_acknowledge_event_id = 0;
};

using orbit_room_wal_publisher_log_operator =
    atfw::util::distributed_system::wal_log_operator<int64_t, PROJECT_NAMESPACE_ID::DOrbitRoomEventLog,
                                                     orbit_room_wal_publisher_log_action_getter, std::less<int64_t>,
                                                     orbit_room_log_action_hash_t, orbit_room_log_action_equal_t>;

using orbit_room_wal_subscriber_type =
    atfw::util::distributed_system::wal_subscriber<orbit_room_wal_subscriber_private_data,
                                                   PROJECT_NAMESPACE_ID::DUserIDKey, player_key_hash_t,
                                                   player_key_equal_t>;

using orbit_room_wal_publisher_type =
    atfw::util::distributed_system::wal_publisher<orbit_room_storage_type, orbit_room_wal_publisher_log_operator,
                                                  orbit_room_wal_publisher_context, orbit_room*,
                                                  orbit_room_wal_subscriber_type>;

// 工厂：创建房间 WAL 发布者（搭框：仅搭结构，具体 vtable 回调为 TODO 占位）
std::shared_ptr<orbit_room_wal_publisher_type> create_orbit_room_publisher(orbit_room& owner);

// ====== 业务调用接口（搭框） ======
// 房间逻辑（orbit_room / orbit_room_manager / task_action_*）统一通过这些接口调用 WAL。
// 本版接口内部逻辑为空（TODO 占位）。
//
// ===== 用户待办（TODO-USER）：WAL→DTMQ 替换（需求 #6） =====
// 你需要提供 DTMQ 接入实现（接入流程另行提供），并保持本文件业务接口签名不变：
//   - add_event_log：把事件写入 WAL 存储（DTMQ 生产者），事件 id 由 alloc_event_id 分配；
//   - broadcast_events：向订阅者推送增量事件（经 LobbysvrService.orbit_room_event_sync 推送 gamesvr）；
//   - dump：生成房间快照（DOrbitRoomSnapshotData，含 running_data）；
//   - subscribe / unsubscribe：创建/移除订阅者（按 DUserIDKey），subscribe 需下发快照 + 增量；
//   - update_acknowledge：按 acknowledge_event_id 对账推进；
//   - alloc_event_id：改为真实事件 id 分配（当前为内存自增占位）。
// 业务侧接线已全部完成（orbit_room / orbit_room_manager / task_action_* 均已调用本接口），
// DTMQ 就绪后只需在本文件内实现内部逻辑，无需改动业务调用方。
class orbit_room_wal_handle {
 public:
  explicit orbit_room_wal_handle(orbit_room& owner);
  ~orbit_room_wal_handle();

  // 事件 id 分配（TODO-USER：DTMQ 替换时改为真实事件 id 分配）
  int64_t alloc_event_id();
  int64_t get_last_allocated_event_id() const noexcept;

  // 写入一条房间事件（TODO-USER：写入 WAL 存储并广播）
  int32_t add_event_log(rpc::context& ctx, PROJECT_NAMESPACE_ID::DOrbitRoomEventLog&& event_log);

  // 向订阅者广播增量事件（TODO-USER：经 orbit_room_event_sync 推送 gamesvr）
  void broadcast_events(rpc::context& ctx);

  // 生成房间快照（TODO-USER）
  void dump(PROJECT_NAMESPACE_ID::DOrbitRoomSnapshotData& out);

  // 订阅 / 反订阅（TODO-USER：对接 SS subscribe / unsubscribe）
  int32_t subscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                    int64_t acknowledge_event_id);
  int32_t unsubscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);

  // 心跳对账：推进 acknowledge_event_id（TODO-USER）
  int32_t update_acknowledge(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                             int64_t acknowledge_event_id);

  const std::shared_ptr<orbit_room_wal_publisher_type>& get_publisher() const noexcept { return publisher_; }

 private:
  orbit_room* owner_;
  std::shared_ptr<orbit_room_wal_publisher_type> publisher_;
  // 事件 id 内存自增（搭框：DTMQ 替换时改为真实事件 id 分配）
  int64_t event_id_alloc_ = 0;
};
