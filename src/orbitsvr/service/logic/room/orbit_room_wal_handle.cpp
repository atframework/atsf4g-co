// Copyright 2026 atframework
// Created by atsf4g-co battle module migration

#include "logic/room/orbit_room_wal_handle.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <rpc/rpc_context.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

orbit_room_wal_publisher_context::orbit_room_wal_publisher_context(rpc::context& ctx, int32_t& output_result)
    : context(std::make_shared<rpc::context>(ctx)), result_code(output_result) {}

// ====== 工厂：创建房间 WAL 发布者（搭框） ======
// 仅搭建发布者结构（vtable 回调为占位实现），具体事件存储/广播逻辑留待后续（DTMQ 替换）。
std::shared_ptr<orbit_room_wal_publisher_type> create_orbit_room_publisher(orbit_room& owner) {
  using publisher_type = orbit_room_wal_publisher_type;
  using wal_object_type = publisher_type::object_type;
  using wal_result_code = atfw::util::distributed_system::wal_result_code;

  publisher_type::vtable_pointer vt = orbit_room_wal_publisher_log_operator::make_strong<publisher_type::vtable_type>();
  if (!vt) {
    return nullptr;
  }

  // 事件 id 分配计数器（搭框：简单自增占位）
  std::shared_ptr<int64_t> key_alloc = std::make_shared<int64_t>(0);

  // ---- wal_object 必需回调 ----
  vt->get_meta = [](const wal_object_type&, const wal_object_type::log_type& log) -> wal_object_type::meta_result_type {
    // 显式转换为 wal_time_point 精度（system_clock::duration），避免纳秒 time_point 无法隐式转换
    return wal_object_type::meta_result_type::make_success(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::system_clock::from_time_t(log.timepoint().seconds()) +
            std::chrono::nanoseconds{log.timepoint().nanos()}),
        log.event_id(), log.event_case());
  };
  vt->set_meta = [](const wal_object_type&, wal_object_type::log_type& log,
                    const wal_object_type::meta_type& meta) {
    // 搭框：仅占位，事件 id 与时间戳由调用方设置
    std::time_t seconds = std::chrono::system_clock::to_time_t(meta.timepoint);
    int32_t nanos = static_cast<int32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             meta.timepoint - std::chrono::system_clock::from_time_t(seconds))
                                             .count());
    log.mutable_timepoint()->set_seconds(seconds);
    log.mutable_timepoint()->set_nanos(nanos);
    log.set_event_id(meta.log_key);
  };
  vt->get_log_key = [](const wal_object_type&, const wal_object_type::log_type& log) -> wal_object_type::log_key_type {
    return log.event_id();
  };
  vt->allocate_log_key = [key_alloc](wal_object_type&, const wal_object_type::log_type&,
                                     wal_object_type::callback_param_type) -> wal_object_type::log_key_result_type {
    return wal_object_type::log_key_result_type::make_success(++(*key_alloc));
  };

  // ---- 快照/加载（搭框：TODO 占位） ----
  vt->load = [](wal_object_type&, const wal_object_type::storage_type&,
                wal_object_type::callback_param_type) -> wal_result_code { return wal_result_code::kOk; };
  vt->dump = [](const wal_object_type&, wal_object_type::storage_type&,
                wal_object_type::callback_param_type) -> wal_result_code { return wal_result_code::kOk; };

  // ---- 哈希（搭框：简单占位） ----
  vt->get_hash_code = [](const wal_object_type&, const wal_object_type::log_type&) -> wal_object_type::hash_code_type {
    return 0;
  };
  vt->set_hash_code = [](const wal_object_type&, wal_object_type::log_type&, wal_object_type::hash_code_type) {};
  vt->calculate_hash_code = [](const wal_object_type&, wal_object_type::hash_code_type previous_hash_code,
                               const wal_object_type::log_type& log) -> wal_object_type::hash_code_type {
    // 简单线性哈希占位（与最终实现无关，DTMQ 替换时重写）
    size_t ret = std::hash<int64_t>()(log.event_id() + static_cast<int64_t>(previous_hash_code));
    return (0 == ret) ? 1 : ret;
  };

  // ---- 事件回调（搭框：TODO 占位） ----
  vt->on_log_added = [](wal_object_type&, const wal_object_type::log_pointer&) {};
  vt->on_log_removed = [](wal_object_type&, const wal_object_type::log_pointer&) {};

  // ---- 日志 action 处理（搭框：TODO 占位，后续按事件体分发） ----
  vt->default_delegate.action =
      [](wal_object_type&, const wal_object_type::log_type&, wal_object_type::callback_param_type) -> wal_result_code {
    return wal_result_code::kOk;
  };

  // ---- publisher 发送回调（搭框：TODO，后续经 LobbysvrService.orbit_room_event_sync 推送 gamesvr） ----
  vt->send_snapshot = [](publisher_type&, publisher_type::subscriber_iterator,
                         publisher_type::subscriber_iterator,
                         publisher_type::callback_param_type) -> wal_result_code { return wal_result_code::kOk; };
  vt->send_logs = [](publisher_type&, publisher_type::log_const_iterator, publisher_type::log_const_iterator,
                     publisher_type::subscriber_iterator, publisher_type::subscriber_iterator,
                     publisher_type::callback_param_type) -> wal_result_code { return wal_result_code::kOk; };
  vt->subscribe_response = [](publisher_type&, const publisher_type::subscriber_pointer&, wal_result_code,
                              publisher_type::callback_param_type) -> wal_result_code { return wal_result_code::kOk; };
  vt->check_subscriber = [](publisher_type&, const publisher_type::subscriber_pointer&,
                            publisher_type::callback_param_type) -> bool { return true; };
  vt->on_subscriber_request = [](publisher_type&, const publisher_type::subscriber_pointer&,
                                 publisher_type::callback_param_type) {};
  vt->on_subscriber_added = [](publisher_type&, const publisher_type::subscriber_pointer&,
                               publisher_type::callback_param_type) {};
  vt->on_subscriber_removed = [](publisher_type&, const publisher_type::subscriber_pointer&,
                                 atfw::util::distributed_system::wal_unsubscribe_reason,
                                 publisher_type::callback_param_type) {};

  publisher_type::configure_pointer conf = publisher_type::make_configure();
  if (!conf) {
    return nullptr;
  }

  // 搭框：仅创建发布者对象，事件收发逻辑后续（DTMQ 替换）实现
  return publisher_type::create(vt, conf, &owner);
}

// ====== 业务调用接口（搭框：仅声明，逻辑留待后续实现） ======

orbit_room_wal_handle::orbit_room_wal_handle(orbit_room& owner)
    : owner_(&owner), publisher_(create_orbit_room_publisher(owner)) {}

orbit_room_wal_handle::~orbit_room_wal_handle() = default;

int64_t orbit_room_wal_handle::alloc_event_id() {
  // 简单内存自增（搭框：DTMQ 替换时改为真实事件 id 分配）
  return ++event_id_alloc_;
}

int64_t orbit_room_wal_handle::get_last_allocated_event_id() const noexcept {
  return event_id_alloc_;
}

int32_t orbit_room_wal_handle::add_event_log(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                             PROJECT_NAMESPACE_ID::DOrbitRoomEventLog&& event_log) {
  // 搭框：TODO 写入事件并广播（后续走 publisher / DTMQ）
  FWLOGDEBUG("orbit_room_wal_handle::add_event_log TODO, event_id: {}", event_log.event_id());
  return 0;
}

void orbit_room_wal_handle::broadcast_events(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx) {
  // 搭框：TODO 向订阅者广播增量（经 LobbysvrService.orbit_room_event_sync 推送 gamesvr）
  FWLOGDEBUG("orbit_room_wal_handle::broadcast_events TODO");
}

void orbit_room_wal_handle::dump(ATFW_EXPLICIT_UNUSED_ATTR PROJECT_NAMESPACE_ID::DOrbitRoomSnapshotData& out) {
  // 搭框：TODO 生成房间快照
  FWLOGDEBUG("orbit_room_wal_handle::dump TODO");
}

int32_t orbit_room_wal_handle::subscribe(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                         const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                         int64_t acknowledge_event_id) {
  // 搭框：TODO 创建订阅者并下发快照 + 增量事件
  FWLOGDEBUG("orbit_room_wal_handle::subscribe TODO, user: {}:{} ack: {}", user_key.zone_id(), user_key.user_id(),
             acknowledge_event_id);
  return 0;
}

int32_t orbit_room_wal_handle::unsubscribe(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                           const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  // 搭框：TODO 移除订阅者
  FWLOGDEBUG("orbit_room_wal_handle::unsubscribe TODO, user: {}:{}", user_key.zone_id(), user_key.user_id());
  return 0;
}

int32_t orbit_room_wal_handle::update_acknowledge(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                                  const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                                  int64_t acknowledge_event_id) {
  // 搭框：TODO 推进订阅者对账
  FWLOGDEBUG("orbit_room_wal_handle::update_acknowledge TODO, user: {}:{} ack: {}", user_key.zone_id(),
             user_key.user_id(), acknowledge_event_id);
  return 0;
}
