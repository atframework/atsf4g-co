// Copyright 2026 atframework
//
// Shared offline fixture for the teamsvr-room service unit tests (see src/teamsvr/TEAM_ROOM_TEST_PLAN.md).
//
// The fixture boots one atfw::testing::runtime (SS + RESOURCE + DB + HPA) per case and emulates the
// dtmq-proxysvr server side on top of the typed SS mock engine:
//   - subscribe        : acks the subscriber heartbeat, lazily creates the channel (kCreate log) and
//                        pushes the ready snapshot through global_receive_channel_event before the
//                        response, so team_room::await_ready succeeds on the first call.
//   - send_message     : team-room channel (channel_type=11) requests run the real lock-CAS semantics
//                        (mq_channel::compare_and_maybe_reset_lock) and append to a hash-chained
//                        journal; personal-channel requests are captured as DTeamMemberAction records.
//   - update           : applies lock reset + custom/private data save + compaction boundary, mirroring
//                        task_action_update's journal side effects (kResetLock/kUpdateCustomData/kNoop).
//   - reset_lock       : lock CAS with real-value copy-back on conflict.
//   - destroy_channel  : marks the channel destroyed and appends the kDestroy log.
//
// The journal mirrors mq_channel/wal_publisher behavior (monotonic sequences, hash chain over the full
// history, kResetLock appended on every lock change) so room-side ack/compaction scheduling observes
// the same contract as production. Events are delivered back to the real client_subscriber through
// SSChannelEventSync + global_receive_channel_event, followed by the same manager flush that
// task_action_channel_event_sync performs in production.
//
// WAL journal mode (wal_journal_mode=true, set before start()) swaps the typed-SS emulated server side
// for the real in-process mq_channel/wal_publisher layer (same sources the component-dtmq-proxysvr
// tests compile): the five dtmq RPC mock handlers mirror the corresponding task_action_* bodies on a
// directly held writable channel, and log delivery flows through the real wal_publisher vtable
// (publisher_send_snapshot/publisher_send_logs -> rpc::dtmq::channel_event_sync no-wait RPC -> mock
// rule -> client_subscriber::global_receive_channel_event). Only the distribution/forwarding layer
// (make_*_channel + discovery ownership) is bypassed; it stays covered by component-dtmq-proxysvr
// tests. The process keeps the teamsvr_room server-instance config, so mq_channel observes the
// default dtmq_proxysvr_cfg instance (defaults are safe: remove_ttl falls back to 21d inside
// mq_channel and max_events_per_tick=0 means unlimited in wal_publisher::tick).

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/any.h>
#include <google/protobuf/empty.pb.h>

#include <protocol/common/svr.struct.dtmq.common.pb.h>
#include <protocol/config/pb_header_v3.pb.h>
#include <protocol/config/team_room.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/atapp.h>
#include <atframework/testing/mock_db.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_resource.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>
#include <rpc/dtmq/dtmq_algorithm.h>
#include <rpc/team/team_common_api.h>
#include <string/string_format.h>
#include <time/time_utility.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "atframe/atapp_conf.h"  // IWYU pragma: keep
#include "config/extern_service_types.h"
#include "config/logic_config.h"
#include "frame/test_macros.h"  // IWYU pragma: keep
#include "logic/logic_server_setup.h"
#include "logic/room/team_room.h"
#include "logic/room/team_room_manager.h"
#include "rpc/db/local_db_interface.atfw.gen.h"  // IWYU pragma: keep
#include "rpc/dtmq/dtmq_client_subscriber.h"
#include "rpc/dtmq/dtmqproxysvrnotifyservice.atfw.gen.h"  // IWYU pragma: keep
#include "rpc/dtmq/dtmqproxysvrservice.atfw.gen.h"
#include "rpc/rpc_context.h"
#include "rpc/rpc_shared_message.h"
#include "rpc/rpc_utils.h"
#include "utility/protobuf_mini_dumper.h"

#include "config/excel_config_dtmq_index.h"
#include "data/mq_channel.h"
#include "logic/mq_channel_manager.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
// Friend of mq_channel_manager(与 component-dtmq-proxysvr 测试同名但分属不同可执行程序)。
// 这里只保留 teamsvr-room WAL 模式需要的 case 间隔离入口。
class MqChannelManagerUnitTest {
 public:
  // 清空 manager 单例的频道/IO 队列状态;定时器随频道析构移除
  static void clear_all_channels(mq_channel_manager& mgr) noexcept {
    mgr.pending_io_channels_.clear();
    mgr.running_io_channels_.clear();
    mgr.reactive_io_channels_.clear();
    mgr.channels_.clear();
  }
};
#endif

namespace teamsvr_room_test {

// ---- Node ids used as discovery targets ----------------------------------------
// Local node id matches the default runtime_options.app_id so logic_config::get_local_server_id()
// resolves to the local test instance (required by the action layer's consistent-hash routing).
constexpr uint64_t kLocalRoomNodeId = 0x11000001;
constexpr uint64_t kDtmqProxyNodeId = 0x1C0001;

constexpr uint32_t kTeamRoomChannelType = atfw::team::EN_TEAM_CHANNEL_TYPE_TEAM_ROOM;
// 测试队伍的默认区服(与注入的 discovery 节点 zone_id 一致)
constexpr uint32_t kTestZoneId = 1;

// 构造测试用 DTeamKey；队伍以 (zone_id, team_id) 为唯一标识
inline atfw::team::DTeamKey make_team_key(int64_t team_id, uint32_t zone_id = kTestZoneId) {
  atfw::team::DTeamKey key;
  key.set_team_id(team_id);
  key.set_zone_id(zone_id);
  return key;
}

// 测试用 keyed 数据的类型标识: 与生产侧(lobbysvr 经 PackFrom 打包类型化消息)对齐，
// 使条目携带有效数据(type_url 与 payload 均非空)；GAP-09 删除标记语义下
// "type_url 为空或 payload 为空"的条目表示删除该 key(仅 member_update/team_update)
inline constexpr const char* kTestTeamAnyDataTypeUrl() noexcept {
  return "type.googleapis.com/atframework.team.ut_team_any_data";
}

// 向 repeated 共享数据字段追加一条 key-value(类型化 Any: type_url + value 字节)；
// 返回 entry 便于调用方继续设置 permission；传入空 value 表示构造删除标记
inline atfw::team::DTeamAnyDataWithKey* add_team_any_data_entry(
    google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* field, int64_t key, const std::string& value) {
  auto* entry = field->Add();
  entry->set_key(key);
  if (!value.empty()) {
    entry->mutable_value()->mutable_data()->set_type_url(kTestTeamAnyDataTypeUrl());
  }
  entry->mutable_value()->mutable_data()->set_value(value);
  return entry;
}

// 向 repeated 条件数据字段追加一条 key-value(Any 与 add_team_any_data_entry 的类型化格式一致，
// 保证条件比较与存储数据同构)
inline atfw::team::DTeamAnyValueWithKey* add_team_any_value_entry(
    google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyValueWithKey>* field, int64_t key,
    const std::string& value) {
  auto* entry = field->Add();
  entry->set_key(key);
  if (!value.empty()) {
    entry->mutable_value()->set_type_url(kTestTeamAnyDataTypeUrl());
  }
  entry->mutable_value()->set_value(value);
  return entry;
}

// ---- Fixture-wide configure (shortest legal durations, see team_room.config.proto min_value) ----
struct room_test_cfg_values {
  // 乐观锁租约与频道 subscriber_timeout 一致(见下方 excel seed)，续租间隔 = 租约/2
  int32_t lock_lease_seconds = 10;
  int32_t compact_log_start_seconds = 10;
  int32_t compact_log_keep_seconds = 0;  // 0 -> start/2
  int32_t member_offline_expire_seconds = 30;
  int32_t empty_room_destroy_delay_seconds = 5;
  int32_t join_request_expire_seconds = 5;
  int32_t invitation_expire_seconds = 5;
  int32_t member_notification_retry_interval_seconds = 1;
  uint32_t member_notification_retry_times = 2;
  uint32_t compact_log_keep_count = 2;
  int32_t compact_log_keep_percent = 50;
  int32_t compact_log_over_percent = 60;
  // excel 侧频道配置: gc_log_count 是数量维度压缩的基准
  uint32_t channel_gc_log_count = 10;
};

// Per-process unique team ids so cases never share a channel or a room.
// acquire_lock 有路由属主校验(见 team_room::acquire_lock)，只生成按 (zone,team) 一致性哈希
// 路由到本节点的 team id；discovery 未就绪(返回0)时直接返回(如 env.start 前的预取场景)
inline int64_t next_test_team_id() {
  static std::atomic<int64_t> next_id{0x1000001};
  for (;;) {
    int64_t id = next_id.fetch_add(1);
    uint64_t route = rpc::team::team_api::get_teamsvr_room_server_id_of_zone(make_team_key(id));
    if (0 == route || route == kLocalRoomNodeId) {
      return id;
    }
  }
}

// Build a populated dtmq_channel_type.bytes with one channel_type=11 row carrying the fixture's
// DChannelConfigure (see dtmq_test_channel_common.h for the datablocks format).
inline std::string make_team_room_channel_type_bytes(const room_test_cfg_values& values) {
  org::xresloader::pb::xresloader_datablocks blocks;
  blocks.mutable_header()->set_hash_code("rpc-unit-test");
  PROJECT_NAMESPACE_ID::config::ExcelDtmqChannelType item;
  item.set_channel_type(kTeamRoomChannelType);
  item.set_readonly_replicate_count(1);
  auto* configure = item.mutable_channel_configure();
  configure->set_channel_type(kTeamRoomChannelType);
  configure->set_max_log_count(300);
  configure->set_gc_log_count(values.channel_gc_log_count);
  // memory_only: WAL 模式直接以进程内 mq_channel 为权威 journal(计划 3.2 节)，不依赖 DB 持久化；
  // fake journal 用例不读取这些服务端字段。gc_expire_duration 必须显式给值，0 会让真实
  // wal_object::gc 把全部历史日志当过期裁剪
  configure->set_memory_only(true);
  configure->mutable_gc_expire_duration()->set_seconds(86400);
  configure->mutable_heartbeat_interval()->set_seconds(300);
  configure->mutable_heartbeat_retry_interval()->set_seconds(60);
  configure->mutable_subscriber_timeout()->set_seconds(values.lock_lease_seconds);
  blocks.add_data_block(item.SerializeAsString());
  return blocks.SerializeAsString();
}

// 进程级测试配置(loader 必须是无捕获 lambda，转成函数指针；本测试目标内所有用例共用同一组值)
inline room_test_cfg_values& mutable_process_test_cfg_values() {
  static room_test_cfg_values values;
  return values;
}

// 进程级虚拟时间基线: client_subscriber 的内部时间轮与共享层跨用例存活，
// 其 last_tick 会停留在之前用例推进过的"未来"；如果下一个用例的虚拟时间回退，
// tick() 将不再推进(定时器永不触发)。每个用例把基线单调前移，保证时间永不回退。
inline std::chrono::system_clock::duration& mutable_process_time_floor() {
  static std::chrono::system_clock::duration value{std::chrono::system_clock::duration::zero()};
  return value;
}

// 进程级虚拟时间的历史最大偏移: 所有用例设置过的最大 now 偏移。基线按固定步长前移不足以覆盖
// 单个用例内部的大偏移(如持锁租约 3600s 的 guard): 该用例在 guard 内驱动 tick 后，
// 进程级时间轮 last_tick 已越过"基线+步长"，后续用例的虚拟时间低于它时定时器不再触发。
inline std::chrono::system_clock::duration& mutable_process_time_max_offset() {
  static std::chrono::system_clock::duration value{std::chrono::system_clock::duration::zero()};
  return value;
}

// 在每次前移虚拟时间后调用，更新进程级历史最大偏移
inline void record_process_time_max_offset() {
  auto& max_offset = mutable_process_time_max_offset();
  const auto offset = atfw::util::time::time_utility::get_global_now_offset();
  if (offset > max_offset) {
    max_offset = offset;
  }
}

// Register the teamsvr_room server-instance config loader (same shape as team_room_main.cpp) with the
// shortest legal durations so timer-driven behaviors are reachable via the now-offset driver. Must be
// called in setup_callback (before app init). Captureless lambda converts to the function pointer.
inline void register_teamsvr_room_config_loader(const room_test_cfg_values& values) {
  mutable_process_test_cfg_values() = values;
  logic_config::me()->set_server_instance_config_loader(
      [](atfw::atapp::app& app_, logic_config& /*cfg*/, logic_config::server_instance_config_ptr& to) {
        const room_test_cfg_values& test_values = mutable_process_test_cfg_values();
        auto config_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::config::teamsvr_room_cfg>();
        app_.parse_configures_into(*config_ptr, "teamsvr_room", "ATAPP_TEAMSVR_ROOM");

        // 测试用最短合法值(不低于协议 min_value)，避免长时间等待
        config_ptr->mutable_compact_log_start_time()->set_seconds(test_values.compact_log_start_seconds);
        config_ptr->mutable_compact_log_keep_time()->set_seconds(test_values.compact_log_keep_seconds);
        config_ptr->set_compact_log_over_percent(test_values.compact_log_over_percent);
        config_ptr->set_compact_log_keep_percent(test_values.compact_log_keep_percent);
        config_ptr->set_compact_log_keep_count(test_values.compact_log_keep_count);
        config_ptr->mutable_member_offline_expire()->set_seconds(test_values.member_offline_expire_seconds);
        config_ptr->mutable_empty_room_destroy_delay()->set_seconds(test_values.empty_room_destroy_delay_seconds);
        config_ptr->mutable_join_request_expire()->set_seconds(test_values.join_request_expire_seconds);
        config_ptr->mutable_invitation_expire()->set_seconds(test_values.invitation_expire_seconds);
        config_ptr->mutable_member_channel_notification_retry_interval()->set_seconds(
            test_values.member_notification_retry_interval_seconds);
        config_ptr->set_member_channel_notification_retry_times(test_values.member_notification_retry_times);

        to = atfw::util::memory::static_pointer_cast<google::protobuf::Message>(config_ptr);
      });
}

// The framework-generated shared subscriber key ("server:<local_server_name>"; see
// dtmq_test_client_subscriber.cpp). event_sync.subscriber_keys must include it for
// global_receive_channel_event to forward to the room's subscriber.
inline std::string shared_subscriber_key_for() {
  if (nullptr != logic_config::me() && !logic_config::me()->get_local_server_name().empty()) {
    return std::string("server:") + std::string(logic_config::me()->get_local_server_name());
  }
  uint64_t server_id = (nullptr != logic_config::me()) ? logic_config::me()->get_local_server_id() : 0;
  return std::string("server:") + std::to_string(server_id);
}

// RAII guard advancing the global now offset (time_utility::now() and everything derived from it,
// including the room manager timer wheel and the fake journal log timestamps). Task/runtime timeouts
// run on sys_now() and are not affected. Restores the previous offset on destruction.
class global_now_offset_guard {
 public:
  global_now_offset_guard() : previous_(atfw::util::time::time_utility::get_global_now_offset()) {}
  explicit global_now_offset_guard(std::chrono::system_clock::duration advance_by)
      : previous_(atfw::util::time::time_utility::get_global_now_offset()) {
    atfw::util::time::time_utility::set_global_now_offset(previous_ + advance_by);
    record_process_time_max_offset();
  }

  static void advance(std::chrono::system_clock::duration by) {
    atfw::util::time::time_utility::set_global_now_offset(atfw::util::time::time_utility::get_global_now_offset() + by);
    record_process_time_max_offset();
  }

  ~global_now_offset_guard() { atfw::util::time::time_utility::set_global_now_offset(previous_); }

  global_now_offset_guard(const global_now_offset_guard&) = delete;
  global_now_offset_guard& operator=(const global_now_offset_guard&) = delete;

 private:
  std::chrono::system_clock::duration previous_;
};

// One captured player-personal-channel DTeamMemberAction (team_room::send_member_action side effect).
struct personal_message_record {
  atfw::dtmq::DChannelIdKey channel;
  atfw::team::DTeamMemberAction action;
};

// Captured update request (for compact/snapshot content assertions).
struct update_request_record {
  atfw::dtmq::SSChannelUpdateReq request;
};

// In-process emulation of one dtmq team-room channel (journal + lock + custom/private snapshot).
// Mirrors mq_channel's observable contract: monotonic sequence allocation, hash chain over the whole
// history (compaction only drops cache entries, never resets the chain), kResetLock log on every lock
// change, custom data change -> kUpdateCustomData log, private data change -> kNoop log.
class fake_team_room_channel {
 public:
  explicit fake_team_room_channel(atfw::team::DTeamKey team_key) : team_key_(std::move(team_key)) {
    channel_key_.CopyFrom(rpc::team::team_api::make_team_room_channel_key(team_key_));
  }

  const atfw::team::DTeamKey& team_key() const noexcept { return team_key_; }
  int64_t team_id() const noexcept { return team_key_.team_id(); }
  const atfw::dtmq::DChannelIdKey& channel_key() const noexcept { return channel_key_; }

  bool is_created() const noexcept { return created_; }
  bool is_destroyed() const noexcept { return destroyed_; }
  int64_t create_sequence() const noexcept { return create_sequence_; }
  int64_t destroy_sequence() const noexcept { return destroy_sequence_; }
  int64_t last_sequence() const noexcept { return last_sequence_; }
  uint64_t last_hash() const noexcept { return last_hash_; }
  int64_t last_removed_sequence() const noexcept { return last_removed_sequence_; }

  const ::atfw::dtmq::DChannelOptimisticLock& lock() const noexcept { return lock_; }
  ::atfw::dtmq::DChannelOptimisticLock& mutable_lock() noexcept { return lock_; }

  const google::protobuf::Any& custom_data() const noexcept { return custom_data_; }
  const google::protobuf::Any& private_data() const noexcept { return private_data_; }
  int64_t custom_data_sequence() const noexcept { return custom_data_sequence_; }
  int64_t private_data_sequence() const noexcept { return private_data_sequence_; }

  // 直接注入快照数据(INF/RCV 用例预置状态)；sequence 钉在最新日志上
  void set_custom_data(const google::protobuf::Message& data) {
    (void)custom_data_.PackFrom(data);
    custom_data_sequence_ = (std::max)(custom_data_sequence_, last_sequence_);
  }
  void set_private_data(const google::protobuf::Message& data) {
    (void)private_data_.PackFrom(data);
    private_data_sequence_ = (std::max)(private_data_sequence_, last_sequence_);
  }

  // ---- 调用计数(权限失败零写入门禁的观察点) ----
  size_t send_message_calls() const noexcept { return send_message_calls_; }
  size_t update_calls() const noexcept { return update_calls_; }
  size_t reset_lock_calls() const noexcept { return reset_lock_calls_; }
  size_t destroy_calls() const noexcept { return destroy_calls_; }
  const std::vector<update_request_record>& update_requests() const noexcept { return update_requests_; }

  // ---- 故障注入(FLT/CRT-04/RCV 崩溃点) ----
  // 对下一次(仅一次)对应 RPC 生效: commit_first=true 时先执行服务端提交(镜像 journal/锁/快照)，
  // 再以 error_code 响应(模拟“已提交但响应丢失/传输失败”)；error_code=0 时表现为提交后无响应。
  struct rpc_fault_t {
    bool present = false;
    bool commit_first = true;
    int32_t error_code = 0;
  };
  rpc_fault_t take_send_fault() {
    rpc_fault_t ret = next_send_fault;
    next_send_fault = rpc_fault_t{};
    return ret;
  }
  rpc_fault_t take_update_fault() {
    rpc_fault_t ret = next_update_fault;
    next_update_fault = rpc_fault_t{};
    return ret;
  }
  rpc_fault_t take_reset_lock_fault() {
    rpc_fault_t ret = next_reset_lock_fault;
    next_reset_lock_fault = rpc_fault_t{};
    return ret;
  }
  rpc_fault_t take_destroy_fault() {
    rpc_fault_t ret = next_destroy_fault;
    next_destroy_fault = rpc_fault_t{};
    return ret;
  }
  rpc_fault_t next_send_fault;
  rpc_fault_t next_update_fault;
  rpc_fault_t next_reset_lock_fault;
  rpc_fault_t next_destroy_fault;

  // ---- journal 观察 ----
  const std::vector<atfw::dtmq::DChannelMessage>& journal() const noexcept { return journal_; }

  // 遍历 journal 中所有 kEvent 日志并解包 DTeamAction；坏 Any 返回 false 并停止。
  template <class Fn>
  bool foreach_team_action(const Fn& fn) const {
    for (const auto& message : journal_) {
      if (message.detail().command_case() != atfw::dtmq::DChannelMessageDetail::kEvent) {
        continue;
      }
      atfw::team::DTeamAction action;
      if (!message.detail().event().UnpackTo(&action)) {
        return false;
      }
      if (!fn(message, action)) {
        break;
      }
    }
    return true;
  }

  // 统计指定 command_case 的日志数量
  size_t count_logs_by_command(atfw::dtmq::DChannelMessageDetail::CommandCase cmd) const {
    size_t ret = 0;
    for (const auto& message : journal_) {
      if (message.detail().command_case() == cmd) {
        ++ret;
      }
    }
    return ret;
  }

  // ---- 服务端行为 ----

  // 惰性创建频道: 追加 kCreate 日志(与真实服务端订阅心跳 auto_create 行为一致)
  bool ensure_created() {
    if (created_) {
      return true;
    }
    created_ = true;
    auto now = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    auto* message = append_log([&now](atfw::dtmq::DChannelMessageDetail& detail) {
      protobuf_copy_message(*detail.mutable_create()->mutable_create_timepoint(), now);
    });
    if (nullptr == message) {
      return false;
    }
    create_sequence_ = message->sequence();
    protobuf_copy_message(create_timepoint_, message->create_timepoint());
    return true;
  }

  // mq_channel::compare_and_maybe_reset_lock 的等价实现。
  // 冲突时把真实锁写回 checker.real_value 并返回 false；成功且带 reset_value 时更新锁并追加
  // kResetLock 日志(仅当锁内容变化时，与 mq_channel::set_lock 一致)。
  bool check_lock(atfw::dtmq::channel_lock_checker& checker) {
    if (!checker.ignore_expect_value()) {
      bool lock_timeout = false;
      if (lock_.has_timeout() && lock_.timeout().seconds() != 0) {
        if (atfw::util::time::time_utility::now() > protobuf_to_system_clock(lock_.timeout())) {
          lock_timeout = true;
        }
      }
      if (!lock_timeout && !(lock_.lock_holder().empty() && checker.allow_empty_real_value()) &&
          lock_.lock_holder() != checker.expect_value().lock_holder()) {
        protobuf_copy_message(*checker.mutable_real_value(), lock_);
        return false;
      }
    }

    if (checker.has_reset_value()) {
      set_lock(checker.reset_value());
    }
    return true;
  }

  void set_lock(const ::atfw::dtmq::DChannelOptimisticLock& lock) {
    if (atfw::atapp::protobuf_equal(lock_, lock)) {
      return;
    }
    protobuf_copy_message(lock_, lock);
    append_log([&lock](atfw::dtmq::DChannelMessageDetail& detail) {
      protobuf_copy_message(*detail.mutable_reset_lock(), lock);
    });
  }

  // 追加一条日志。detail_fill 负责填充 DChannelMessageDetail；forced_sequence_gap 用于 EVT-01 的
  // sequence 缺口场景(跳号)。返回 journal 中的消息指针。
  template <class Fn>
  atfw::dtmq::DChannelMessage* append_log(Fn&& detail_fill, int64_t forced_sequence_gap = 0) {
    journal_.emplace_back();
    auto& message = journal_.back();
    message.set_sequence(last_sequence_ + 1 + (forced_sequence_gap > 0 ? forced_sequence_gap : 0));
    message.set_channel_type(kTeamRoomChannelType);
    *message.mutable_create_timepoint() = protobuf_from_system_clock(atfw::util::time::time_utility::now());
    std::forward<Fn>(detail_fill)(*message.mutable_detail());
    message.set_hash_code(rpc::dtmq::calculate_hash_code(last_hash_, message));
    last_sequence_ = message.sequence();
    last_hash_ = message.hash_code();
    return &journal_.back();
  }

  // 从 journal 中移除指定日志并重算后续 hash 链(模拟服务端 GC/修复掉损坏日志)。
  // 不更新 last_removed_sequence_(由调用方按需设置 compact 边界)。返回是否找到并移除。
  bool erase_log(int64_t sequence) {
    for (auto iter = journal_.begin(); iter != journal_.end(); ++iter) {
      if (iter->sequence() != sequence) {
        continue;
      }
      iter = journal_.erase(iter);
      // 重算剩余日志的 hash 链
      uint64_t prev_hash = 0;
      for (iter = journal_.begin(); iter != journal_.end(); ++iter) {
        iter->set_hash_code(rpc::dtmq::calculate_hash_code(prev_hash, *iter));
        prev_hash = iter->hash_code();
      }
      last_hash_ = journal_.empty() ? 0 : journal_.back().hash_code();
      last_sequence_ = journal_.empty() ? 0 : journal_.back().sequence();
      return true;
    }
    return false;
  }

  // task_action_update 的服务端镜像: 锁检查由调用方完成后调用。
  // 日志副作用顺序与真实服务端一致: 先 set custom/private(此时 sequence 指向旧日志)，
  // 追加通知日志(kUpdateCustomData 或 kNoop)，再按真实服务端的 reset_*__sequence 规则回置。
  void apply_update(const atfw::dtmq::SSChannelUpdateReq& req) {
    update_requests_.push_back(update_request_record{req});
    ++update_calls_;

    bool has_changed_custom_data = false;
    if (req.has_custom_data()) {
      has_changed_custom_data = !atfw::atapp::protobuf_equal(custom_data_, req.custom_data());
      protobuf_copy_message(custom_data_, req.custom_data());
    }
    bool has_changed_private_data = false;
    if (req.has_private_data()) {
      has_changed_private_data = !atfw::atapp::protobuf_equal(private_data_, req.private_data());
      protobuf_copy_message(private_data_, req.private_data());
    }

    if (has_changed_custom_data && !req.custom_data_skip_notify()) {
      append_log([](atfw::dtmq::DChannelMessageDetail& detail) { detail.set_update_custom_data(true); });
      // reset_custom_data_sequence: custom data 覆盖到最新日志
      custom_data_sequence_ = last_sequence_;
    } else if (has_changed_custom_data || has_changed_private_data) {
      append_log([](atfw::dtmq::DChannelMessageDetail& detail) { detail.set_noop(true); });
    }
    if (has_changed_private_data) {
      // reset_private_data_sequence: private data 覆盖到最新日志
      private_data_sequence_ = last_sequence_;
    }

    // compact: 严格小于 compact_sequence 的日志从缓存移除，边界日志保留(开区间语义，
    // 与 mq_channel::compact_sequence / wal log_key_compare 严格小于一致);last_removed_sequence_ 记录边界值
    if (req.compact_sequence() > last_removed_sequence_) {
      last_removed_sequence_ = req.compact_sequence();
      size_t remove_count = 0;
      for (const auto& message : journal_) {
        if (message.sequence() < last_removed_sequence_) {
          ++remove_count;
        } else {
          break;
        }
      }
      if (remove_count > 0) {
        journal_.erase(journal_.begin(), journal_.begin() + static_cast<std::ptrdiff_t>(remove_count));
      }
    }
  }

  // task_action_destroy_channel 的服务端镜像
  void apply_destroy() {
    ++destroy_calls_;
    if (destroyed_) {
      return;
    }
    destroyed_ = true;
    auto* message = append_log([](atfw::dtmq::DChannelMessageDetail& detail) {
      detail.mutable_destroy()->mutable_removed_timepoint()->set_seconds(0);
    });
    if (nullptr != message) {
      destroy_sequence_ = message->sequence();
      protobuf_copy_message(destroy_timepoint_, message->create_timepoint());
      protobuf_copy_message(*message->mutable_detail()->mutable_destroy()->mutable_removed_timepoint(),
                            destroy_timepoint_);
    }
  }

  // 注入损坏的 custom Any(RCV-04: 类型 URL 匹配但 body 非法)
  void set_corrupt_custom_data(const std::string& type_url, const std::string& corrupt_value) {
    custom_data_.set_type_url(type_url);
    custom_data_.set_value(corrupt_value);
    custom_data_sequence_ = (std::max)(custom_data_sequence_, last_sequence_);
  }

  // ---- 下发进度 ----
  bool snapshot_pushed() const noexcept { return snapshot_pushed_; }
  void mark_snapshot_pushed(int64_t pushed_sequence) {
    snapshot_pushed_ = true;
    mark_pushed(pushed_sequence);
  }
  int64_t last_pushed_sequence() const noexcept { return last_pushed_sequence_; }
  void mark_pushed(int64_t pushed_sequence) {
    last_pushed_sequence_ = (std::max)(last_pushed_sequence_, pushed_sequence);
    last_pushed_custom_seq_ = custom_data_sequence_;
    last_pushed_private_seq_ = private_data_sequence_;
    last_pushed_removed_seq_ = last_removed_sequence_;
    last_pushed_destroyed_ = destroyed_;
  }
  // 是否存在未下发的新日志或快照元数据变化
  bool has_pending_changes() const {
    if (last_sequence_ > last_pushed_sequence_) {
      return true;
    }
    if (custom_data_sequence_ != last_pushed_custom_seq_ || private_data_sequence_ != last_pushed_private_seq_ ||
        last_removed_sequence_ != last_pushed_removed_seq_ || destroyed_ != last_pushed_destroyed_) {
      return true;
    }
    return false;
  }

  atfw::dtmq::DChannelSnapshot dump_snapshot() const {
    atfw::dtmq::DChannelSnapshot snapshot;
    auto* metadata = snapshot.mutable_channel_metadata();
    metadata->mutable_channel_key()->CopyFrom(channel_key_);
    metadata->set_create_sequence(create_sequence_);
    protobuf_copy_message(*metadata->mutable_create_timepoint(), create_timepoint_);
    metadata->set_last_sequence(last_sequence_);
    metadata->set_last_hash_code(last_hash_);
    if (destroyed_) {
      metadata->set_destroy_sequence(destroy_sequence_);
      protobuf_copy_message(*metadata->mutable_destroy_timepoint(), destroy_timepoint_);
    }
    if (!custom_data_.type_url().empty()) {
      metadata->set_custom_data_sequence(custom_data_sequence_);
      metadata->mutable_custom_data()->CopyFrom(custom_data_);
    }
    for (const auto& message : journal_) {
      *snapshot.add_messages() = message;
    }
    protobuf_copy_message(*snapshot.mutable_lock(), lock_);
    snapshot.mutable_channel_runtime()->set_last_removed_sequence(last_removed_sequence_);
    if (!private_data_.type_url().empty()) {
      snapshot.mutable_channel_runtime()->set_private_data_sequence(private_data_sequence_);
      snapshot.mutable_channel_runtime()->mutable_private_data()->CopyFrom(private_data_);
    }
    return snapshot;
  }

  // 用当前 channel 元数据填充增量 event_sync 外壳(EVT-10 自定义批次的合法外壳)。
  // with_data=true 时携带 custom/private data(模拟数据变化后的服务端批次)
  void fill_event_sync_metadata(atfw::dtmq::SSChannelEventSync& event_sync, bool with_data = false) const {
    auto* metadata = event_sync.mutable_channel_metadata();
    metadata->mutable_channel_key()->CopyFrom(channel_key_);
    metadata->set_create_sequence(create_sequence_);
    protobuf_copy_message(*metadata->mutable_create_timepoint(), create_timepoint_);
    metadata->set_last_sequence(last_sequence_);
    metadata->set_last_hash_code(last_hash_);
    if (destroyed_) {
      metadata->set_destroy_sequence(destroy_sequence_);
      protobuf_copy_message(*metadata->mutable_destroy_timepoint(), destroy_timepoint_);
    }
    if (with_data && !custom_data_.type_url().empty()) {
      metadata->set_custom_data_sequence(custom_data_sequence_);
      metadata->mutable_custom_data()->CopyFrom(custom_data_);
    }
    auto* runtime_data = event_sync.mutable_channel_runtime();
    runtime_data->set_last_removed_sequence(last_removed_sequence_);
    if (with_data && !private_data_.type_url().empty()) {
      runtime_data->set_private_data_sequence(private_data_sequence_);
      runtime_data->mutable_private_data()->CopyFrom(private_data_);
    }
  }

  // 复制 journal 中指定 sequence 的日志(自定义批次用；不存在返回 nullptr)
  const atfw::dtmq::DChannelMessage* find_journal_message(int64_t sequence) const {
    for (const auto& message : journal_) {
      if (message.sequence() == sequence) {
        return &message;
      }
    }
    return nullptr;
  }

 private:
  atfw::team::DTeamKey team_key_;
  atfw::dtmq::DChannelIdKey channel_key_;

  std::vector<atfw::dtmq::DChannelMessage> journal_;
  int64_t last_sequence_ = 0;
  uint64_t last_hash_ = 0;

  bool created_ = false;
  int64_t create_sequence_ = 0;
  google::protobuf::Timestamp create_timepoint_;

  bool destroyed_ = false;
  int64_t destroy_sequence_ = 0;
  google::protobuf::Timestamp destroy_timepoint_;

  ::atfw::dtmq::DChannelOptimisticLock lock_;
  google::protobuf::Any custom_data_;
  int64_t custom_data_sequence_ = 0;
  google::protobuf::Any private_data_;
  int64_t private_data_sequence_ = 0;
  int64_t last_removed_sequence_ = 0;

  bool snapshot_pushed_ = false;
  int64_t last_pushed_sequence_ = 0;
  int64_t last_pushed_custom_seq_ = 0;
  int64_t last_pushed_private_seq_ = 0;
  int64_t last_pushed_removed_seq_ = 0;
  bool last_pushed_destroyed_ = false;

  size_t send_message_calls_ = 0;
  size_t update_calls_ = 0;
  size_t reset_lock_calls_ = 0;
  size_t destroy_calls_ = 0;
  std::vector<update_request_record> update_requests_;

  friend class room_test_env;
};

// 一次性确定性响应挂起门(CON/LCK 乱序用例): arm 后下一次对应 RPC 的 mock handler 在“提交完成、
// 响应已填好”之后经 rpc::custom_wait 挂起,响应内容冻结在提交时刻;测试以
// release_*_response_gate(rpc::custom_resume) 确定性放行。挂起/放行都是显式事件,
// 不依赖真实 sleep、固定 pump 次数或调度时序作为业务 oracle。
struct response_gate_t {
  bool armed = false;                        // 下一次对应 RPC 挂起响应(armed 后未被消费会使 stop 失败)
  char token = 0;                            // custom_wait 的 type_address 取 &token(地址即唯一标识)
  uint64_t sequence = 0;                     // 最近一次挂起的 resume 匹配序号
  task_type_trait::id_type parked_task = 0;  // 挂起中的 mock handler 任务 id(0=无挂起)
};

// 一次性 preempt 响应故障的种类(引擎投递层,fixture 服务端语义不变: handler 照常提交)
enum class response_fault_kind : uint8_t {
  kMalformedTypeUrl,  // 投递空 type_url 的响应(调用方不得当成功)
  kMalformedBody,     // 投递无法解析的响应体
  kNoResponse,        // 不投递响应(调用方直至 RPC/任务超时)
};

// 每个用例一个 env: 启动 runtime、注册 mock 规则、初始化并清理 team_room_manager。
class room_test_env {
 public:
  explicit room_test_env(room_test_cfg_values cfg = room_test_cfg_values{}) : cfg_(cfg) {}
  ~room_test_env() {
    if (runtime_ && runtime_->is_running()) {
      runtime_->stop();
    }
    if (!team_room_manager::is_instance_destroyed()) {
      team_room_manager::me()->clear();
    }
    wal_cleanup_for_case();
  }

  room_test_env(const room_test_env&) = delete;
  room_test_env& operator=(const room_test_env&) = delete;

  const room_test_cfg_values& cfg() const noexcept { return cfg_; }
  atframework::testing::runtime& runtime() noexcept { return *runtime_; }

  // 覆盖本进程节点 id(默认 kLocalRoomNodeId, 即本进程就是房间路由属主节点)。
  // SDK 封装路由用例把它改成非房间节点，使"本地哈希目标"的 RPC 也经过 mock transport 以便断言
  uint64_t app_id_override = 0;

  // 启动 fixture。失败时打印诊断并返回 false(调用方应直接 return 结束用例)。
  bool start() {
    runtime_ = std::make_unique<atframework::testing::runtime>();
    atframework::testing::runtime_options options;
    options.features = {atframework::testing::feature::ss, atframework::testing::feature::resource,
                        atframework::testing::feature::db, atframework::testing::feature::hpa};
    options.app_id = 0 == app_id_override ? kLocalRoomNodeId : app_id_override;
    room_test_cfg_values cfg_copy = cfg_;
    options.setup_callback = [cfg_copy](atframework::testing::runtime& rt) -> int {
      rt.resource().set_file("dtmq_channel_type.bytes", make_team_room_channel_type_bytes(cfg_copy));
      rt.resource().set_version("0.10.0.1");
      register_teamsvr_room_config_loader(cfg_copy);
      return 0;
    };

    if (0 != runtime_->start(options)) {
      CASE_MSG_INFO() << "runtime start failed: " << runtime_->get_diagnostic() << '\n';
      return false;
    }

    if (!setup_discovery_nodes()) {
      CASE_MSG_INFO() << "discovery injection failed: " << runtime_->get_diagnostic() << '\n';
      return false;
    }

    if (!register_server_rules()) {
      CASE_MSG_INFO() << "server rule registration failed: " << runtime_->ss().get_diagnostic() << '\n';
      return false;
    }

    if (wal_journal_mode && !setup_wal_journal()) {
      CASE_MSG_INFO() << "wal journal setup failed: " << runtime_->ss().get_diagnostic() << '\n';
      return false;
    }

    if (0 != team_room_manager::me()->init()) {
      CASE_MSG_INFO() << "team_room_manager init failed\n";
      return false;
    }

    // 时间轮是进程级单例且 init 幂等(last_tick_ 只前进不后退): 大时间偏移用例会把它推到未来，
    // 导致后续用例的房间定时器被钳制到 last_tick_+1 永不触发。每个环境启动时重建时间轮隔离。
    // 注意必须在时间基线前移之前重置: 重置后 last_tick_=当前真实now(即基线前移前)，
    // 过期/立即触发(==now)的定时器项才不会被钳制到"未来1秒"而错过本次驱动
    team_room_manager::me()->reset_timer_wheel_for_test();

    // 单调前移虚拟时间基线(见 mutable_process_time_floor 注释): 必须大于所有用例已推进到的
    // 最大偏移(含上个用例 guard 内的大偏移)，否则进程级订阅者时间轮 last_tick 仍停留在未来，
    // 本用例驱动 tick 时订阅重试等定时器不会触发
    record_process_time_max_offset();
    auto& time_floor = mutable_process_time_floor();
    time_floor = (std::max)(time_floor, mutable_process_time_max_offset()) + std::chrono::seconds{120};
    atfw::util::time::time_utility::set_global_now_offset(time_floor);
    return true;
  }

  int stop() {
    bool has_unconsumed_fault = !personal_send_plan_.empty() || subscribe_fail_times_ > 0;
    has_unconsumed_fault = has_unconsumed_fault || update_response_gate.armed || reset_lock_response_gate.armed ||
                           send_message_response_gate.armed || gate_parked(update_response_gate) ||
                           gate_parked(reset_lock_response_gate) || gate_parked(send_message_response_gate);
    for (const auto& channel_pair : channels_) {
      if (!channel_pair.second) {
        continue;
      }
      const auto& channel = *channel_pair.second;
      has_unconsumed_fault = has_unconsumed_fault || channel.next_send_fault.present ||
                             channel.next_update_fault.present || channel.next_reset_lock_fault.present ||
                             channel.next_destroy_fault.present;
    }
    if (has_unconsumed_fault) {
      CASE_MSG_INFO() << "room test stopped with an unconsumed fault script\n";
    }

    int ret = 0;
    if (runtime_ && runtime_->is_running()) {
      ret = runtime_->stop();
    }
    if (0 != ret) {
      return ret;
    }
    return has_unconsumed_fault ? -1 : 0;
  }

  // ---- fake channel registry ----
  fake_team_room_channel& channel(int64_t team_id, uint32_t zone_id = kTestZoneId) {
    return channel(make_team_key(team_id, zone_id));
  }

  fake_team_room_channel& channel(const atfw::team::DTeamKey& team_key) {
    auto& ptr = channels_[team_key];
    if (!ptr) {
      ptr = std::make_shared<fake_team_room_channel>(team_key);
    }
    return *ptr;
  }

  fake_team_room_channel* find_channel_by_key(const atfw::dtmq::DChannelIdKey& key) {
    if (key.channel_type() != kTeamRoomChannelType) {
      return nullptr;
    }
    // 频道 ID 由 team_api::make_team_room_channel_key 生成的标准单播格式: "channel:<type>:<zone_id>:<team_id>"
    // (数字实例段无前缀; 名字实例段带 $ 前缀, 不是房间频道)
    try {
      const std::string& channel_id = key.channel_id();
      size_t type_begin = channel_id.find(':');
      size_t zone_begin = std::string::npos == type_begin ? type_begin : channel_id.find(':', type_begin + 1);
      size_t team_begin = std::string::npos == zone_begin ? zone_begin : channel_id.find(':', zone_begin + 1);
      if (std::string::npos == team_begin || channel_id.find(':', team_begin + 1) != std::string::npos ||
          channel_id.substr(0, type_begin) != "channel") {
        return nullptr;
      }
      std::string type_text = channel_id.substr(type_begin + 1, zone_begin - type_begin - 1);
      std::string zone_text = channel_id.substr(zone_begin + 1, team_begin - zone_begin - 1);
      std::string team_text = channel_id.substr(team_begin + 1);
      if (!team_text.empty() && team_text.front() == '$') {
        return nullptr;
      }
      auto channel_type = std::stoul(type_text);
      auto zone_id = std::stoul(zone_text);
      auto team_id = std::stoll(team_text);
      if (type_text != std::to_string(channel_type) || zone_text != std::to_string(zone_id) ||
          team_text != std::to_string(team_id) || channel_type != key.channel_type()) {
        return nullptr;
      }
      return &channel(make_team_key(team_id, static_cast<uint32_t>(zone_id)));
    } catch (...) {
      return nullptr;
    }
  }

  // ---- personal channel captures ----
  const std::vector<personal_message_record>& personal_messages() const noexcept { return personal_messages_; }
  size_t personal_message_count() const noexcept { return personal_messages_.size(); }

  // 个人频道发送故障编排: send 使用 no-wait stream，不存在业务响应；每次发送弹出队首。
  // commit_first=true 表示远端已接收消息，随后 handler 以 error_code 结束；false 表示处理前丢弃。
  // 队列为空时正常接收。
  void queue_personal_send_result(bool commit_first, int32_t error_code) {
    personal_send_plan_.emplace_back(commit_first, error_code);
  }

  // subscribe 故障编排: 前 N 次 subscribe 心跳直接返回错误(快速失败语义，模拟暂时无 DTMQ 节点/心跳超时)
  void fail_subscribe_times(uint32_t times, int32_t error_code) {
    subscribe_fail_times_ = times;
    subscribe_fail_code_ = error_code;
  }

  // ---- 响应挂起门(见 response_gate_t): 只作用于 fake 层 team-room 频道分支 ----
  response_gate_t update_response_gate;
  response_gate_t reset_lock_response_gate;
  response_gate_t send_message_response_gate;

  static bool gate_parked(const response_gate_t& gate) noexcept { return 0 != gate.parked_task; }

  // 放行挂起中的响应; 无挂起返回 false。放行后 handler 任务在下一轮泵完成,响应随即投递
  static bool release_gate(response_gate_t& gate) {
    if (0 == gate.parked_task) {
      return false;
    }
    auto resume_data = dispatcher_make_default<dispatcher_resume_data_type>();
    resume_data.message.message_type = reinterpret_cast<uintptr_t>(&gate.token);
    resume_data.sequence = gate.sequence;
    return 0 == rpc::custom_resume(gate.parked_task, resume_data);
  }

  // ---- 一次性 preempt 响应故障(mock_ss preempt 规则: 插队匹配一次后回落到默认规则) ----
  // fixture 服务端语义由同一 impl 执行(照常提交/照常拒绝),故障只作用于响应投递层。
  // 仅支持 fake 层(WAL 模式下 team-room 分支由真实 mq_channel 处理,不走这里)。
  atframework::testing::ss_rule_handle inject_update_response_fault_once(response_fault_kind kind) {
    preempt_handler_t handler = [](room_test_env* self, const atframework::testing::ss_request_view& request,
                                   google::protobuf::Message& response) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
          self->fake_handle_update(static_cast<const atfw::dtmq::SSChannelUpdateReq&>(request.body),
                                   static_cast<atfw::dtmq::SSChannelUpdateRsp&>(response), request.context)));
    };
    return inject_response_fault_once(
        rpc::dtmq::packer::get_full_name_of_update(), atfw::dtmq::SSChannelUpdateReq::descriptor()->full_name(),
        atfw::dtmq::SSChannelUpdateRsp::descriptor()->full_name(), kind, std::move(handler));
  }

  atframework::testing::ss_rule_handle inject_reset_lock_response_fault_once(response_fault_kind kind) {
    preempt_handler_t handler = [](room_test_env* self, const atframework::testing::ss_request_view& request,
                                   google::protobuf::Message& response) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
          self->fake_handle_reset_lock(static_cast<const atfw::dtmq::SSChannelResetLockReq&>(request.body),
                                       static_cast<atfw::dtmq::SSChannelResetLockRsp&>(response), request.context)));
    };
    return inject_response_fault_once(
        rpc::dtmq::packer::get_full_name_of_reset_lock(), atfw::dtmq::SSChannelResetLockReq::descriptor()->full_name(),
        atfw::dtmq::SSChannelResetLockRsp::descriptor()->full_name(), kind, std::move(handler));
  }

  atframework::testing::ss_rule_handle inject_send_message_response_fault_once(response_fault_kind kind) {
    preempt_handler_t handler = [](room_test_env* self, const atframework::testing::ss_request_view& request,
                                   google::protobuf::Message& response) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(self->fake_handle_send_message(
          static_cast<const atfw::dtmq::SSChannelSendMessageReq&>(request.body),
          static_cast<atfw::dtmq::SSChannelSendMessageRsp&>(response), request.context)));
    };
    return inject_response_fault_once(rpc::dtmq::packer::get_full_name_of_send_message(),
                                      atfw::dtmq::SSChannelSendMessageReq::descriptor()->full_name(),
                                      atfw::dtmq::SSChannelSendMessageRsp::descriptor()->full_name(), kind,
                                      std::move(handler));
  }

  // ---- WAL journal mode(真实 mq_channel/wal_publisher 层，计划 §3.2/§4.6) ----
  // 必须在 start() 前设置。开启后五个 dtmq RPC mock 的 team-room 频道分支改由进程内真实
  // mq_channel(wal_publisher + wal_object)执行，日志下发走真实 vtable -> channel_event_sync。
  bool wal_journal_mode = false;

  // 捕获的 SSChannelEventSync 下发批次(按到达顺序;WAL-01/02 断言快照/增量判定与内容用)
  struct wal_event_sync_record {
    atfw::dtmq::SSChannelEventSync batch;
  };
  const std::vector<wal_event_sync_record>& wal_event_batches() const noexcept { return wal_event_batches_; }
  void wal_clear_event_batches() noexcept { wal_event_batches_.clear(); }

  using wal_channel_ptr_t = mq_channel_manager::mq_channel_ptr_type;

  // 查找 WAL 频道(不创建)
  wal_channel_ptr_t wal_find_channel(const atfw::team::DTeamKey& team_key) const {
    return wal_find_channel_by_key(rpc::team::team_api::make_team_room_channel_key(team_key));
  }

  // 协程内获取(或创建)可写频道: 直连构造 + manager::add_channel(镜像 component-dtmq 测试)，
  // memory_only 配置下 writable_init 短路升级、无 DB;首次创建写 kCreate 日志。分布归属与转发
  // 属于 component-dtmq-proxysvr 测试职责，这里不做
  rpc::result_code_type wal_ensure_channel(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                           bool create_if_absent = true) {
    auto channel_key = rpc::team::team_api::make_team_room_channel_key(team_key);
    if (!create_if_absent && !wal_find_channel_by_key(channel_key)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
    }
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(wal_ensure_channel_by_key(ctx, channel_key)));
  }

  // 协程内直接向真实 publisher 提交一条 kEvent(DTeamAction) 日志(sequence/hash 由真实
  // allocate_log_key/哈希链分配)。forced_sequence_gap>0 时跳号构造非连续 sequence(EVT-01/WAL-04)；
  // broadcast=false 时不 tick，日志停留在 journal 未向订阅者广播(WAL-06 的"待广播日志"场景)
  static rpc::result_code_type wal_commit_team_action(rpc::context& ctx, const wal_channel_ptr_t& channel,
                                                      const atfw::team::DTeamAction& action,
                                                      int64_t forced_sequence_gap = 0, bool broadcast = true) {
    if (!channel || !channel->is_available()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
    }
    int32_t result = 0;
    mq_channel_wal_object_context param{ctx, result};
    auto message = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                             atfw::dtmq::DChannelMessageDetail::kEvent, param);
    if (!message) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    message->set_channel_type(channel->get_channel_key().channel_type());
    if (forced_sequence_gap > 0) {
      // allocate_log_key 对 sequence>0 的日志保留原值，跳号后哈希链仍按顺序计算
      message->set_sequence(channel->get_last_message_sequence() + 1 + forced_sequence_gap);
    }
    (void)message->mutable_detail()->mutable_event()->PackFrom(action);
    channel->get_wal_publisher().emplace_back_log(std::move(message), param);
    if (broadcast) {
      channel->tick(ctx);
    }
    RPC_RETURN_CODE(result < 0 ? result : 0);
  }

  // 协程内创建一个与现有频道同 key 的空白可写频道(WAL-05/06 的 dump->load 往返目标，镜像
  // writable transfer 后新节点上的频道对象)。不做任何注册: 调用方 load_snapshot 后以
  // wal_swap_channel 激活为唯一权威。空白频道不追加 kCreate 日志(日志随快照载入)
  static rpc::result_code_type wal_make_replacement_channel(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                                            wal_channel_ptr_t& output) {
    auto channel_key = rpc::team::team_api::make_team_room_channel_key(team_key);
    auto configure = excel::get_dtmq_channel_configure(channel_key.channel_type());
    if (!configure) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
    }
    auto channel =
        atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, *configure);
    if (!channel) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    // memory_only + auto_create: 仅升级为 writable(无 DB、不落 kCreate 日志)
    auto init_ret = RPC_AWAIT_CODE_RESULT(channel->writable_init(ctx, true));
    if (init_ret < 0) {
      RPC_RETURN_CODE(init_ret);
    }
    output = std::move(channel);
    RPC_RETURN_CODE(0);
  }

  // 把 load 完成的 replacement 激活为该 channel id 的唯一权威(镜像 transfer 完成后的注册状态):
  // manager::add_channel 按 id 替换旧频道对象，fixture 的 RPC mock 路由同步切换到新频道。
  // 旧频道对象由调用方持有的指针继续存活，用于"旧 publisher 不再接受有效写入"断言
  void wal_swap_channel(rpc::context& ctx, const wal_channel_ptr_t& replacement) {
    if (!replacement) {
      return;
    }
    mq_channel_manager::me()->add_channel(ctx, replacement);
    wal_channels_[replacement->get_channel_key().channel_id()] = replacement;
  }

  // 协程内以指定 checkpoint 对真实 publisher 触发一次订阅决策(WAL-02: 正常 checkpoint 只补后续
  // 日志、hash 不匹配强制 snapshot)。订阅者信息与 room 侧共享订阅者一致
  static rpc::result_code_type wal_resubscribe(rpc::context& ctx, const wal_channel_ptr_t& channel,
                                               int64_t last_sequence, uint64_t last_hash_code) {
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
        wal_resubscribe_as(ctx, channel, shared_subscriber_key_for(), last_sequence, last_hash_code, true)));
  }

  // wal_resubscribe 的多订阅者版本: 以指定订阅者 key 触发订阅决策(WAL-03 的落后/跟得上双订阅者)。
  // 进程内没有该 key 对应的 client_subscriber 时批次由 global_receive_channel_event 记录并丢弃，
  // 不影响批次内容断言。with_private_data 会更新到已存在订阅者(与 room 共享 key 时必须传 true)
  static rpc::result_code_type wal_resubscribe_as(rpc::context& ctx, const wal_channel_ptr_t& channel,
                                                  const std::string& subscriber_key, int64_t last_sequence,
                                                  uint64_t last_hash_code, bool with_private_data) {
    if (!channel || !channel->is_available()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
    }
    atfw::dtmq::channel_subscriber subscriber;
    subscriber.set_subscriber_key(subscriber_key);
    subscriber.set_with_private_data(with_private_data);
    subscriber = wal_redirect_subscriber(subscriber);
    channel->subscribe(ctx, subscriber, last_sequence, last_hash_code, false);
    channel->tick(ctx);
    RPC_RETURN_CODE(0);
  }

  // 泵空真实 vtable 下发的 channel_event_sync 并 flush 房间待发个人通知;房间写回产生的新事件
  // 会继续入队，循环到稳定为止
  int32_t wal_converge(size_t max_rounds = 8) {
    const auto event_sync_name = rpc::dtmq::packer::get_full_name_of_channel_event_sync();
    for (size_t round = 0; round < max_rounds; ++round) {
      if (runtime_->pump_once() < 0) {
        return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
      }
      size_t submitted_events = runtime_->ss().calls(event_sync_name);
      bool completed =
          wait_until([this, submitted_events]() { return wal_event_sync_handler_attempts_ >= submitted_events; });
      CASE_EXPECT_TRUE(completed);
      if (!completed) {
        return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
      }

      int32_t ret = run("wal_flush", [](rpc::context& ctx) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(team_room_manager::me()->flush_pending_channel_message(ctx)));
      });
      if (0 != ret) {
        return ret;
      }
      completed = wait_for_send_message_handlers();
      CASE_EXPECT_TRUE(completed);
      if (!completed) {
        return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
      }

      // 事件应用/flush 期间可能产生新的 channel_event_sync(房间写回)，未稳定则再来一轮
      if (runtime_->ss().calls(event_sync_name) <= submitted_events) {
        return 0;
      }
    }
    CASE_MSG_INFO() << "wal_converge did not stabilize\n";
    return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
  }

  // ---- driving helpers ----

  // 公开的泵驱动等待(并发用例等待挂起门/提交计数等可观测条件; 不得以固定泵次数代替)
  template <class Predicate>
  bool wait_for(Predicate&& predicate, std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
    return wait_until(std::forward<Predicate>(predicate), timeout);
  }

  // run_task + wait 封装。返回任务结果码；基础设施失败(任务未启动/硬超时)返回 INT32_MIN。
  int32_t run(gsl::string_view name, std::function<rpc::result_code_type(rpc::context&)> fn,
              std::chrono::system_clock::duration timeout = std::chrono::seconds{8},
              std::chrono::system_clock::duration hard_timeout = std::chrono::seconds{15}) {
    auto task = runtime_->run_task(name, timeout, std::move(fn));
    if (task.empty()) {
      CASE_MSG_INFO() << "task " << name << " start failed: " << task.get_diagnostic() << '\n';
      return std::numeric_limits<int32_t>::min();
    }
    auto result = runtime_->wait(task, hard_timeout);
    if (!result.task_exited || result.hard_timed_out) {
      CASE_MSG_INFO() << "task " << name << " did not exit cleanly: " << result.diagnostic << '\n';
      return std::numeric_limits<int32_t>::min();
    }
    return result.result_code;
  }

  // 创建房间并等待订阅就绪(订阅心跳期间 mock 推送就绪快照)。
  // WAL journal 模式下事件批由真实 vtable 以 no-wait 异步下发，可能晚于订阅回包到达；
  // client_subscriber 在看到 create 事件/快照前不会就绪(与生产一致)。这里在首次
  // await_ready 因订阅未就绪失败时泵空事件批再重试一次，等价于生产调用方的重试语义
  team_room::ptr_t setup_ready_room(const atfw::team::DTeamKey& team_key) {
    team_room::ptr_t room;
    int32_t ret = run("setup_ready_room", [&team_key, &room](rpc::context& ctx) -> rpc::result_code_type {
      room = team_room_manager::me()->mutable_room(ctx, team_key);
      if (!room) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
      }
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
    });
    if (0 != ret && wal_journal_mode && room && !room->is_subscriber_ready()) {
      if (0 == wal_converge()) {
        ret = run("setup_ready_room_retry", [&room](rpc::context& ctx) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
        });
      }
    }
    if (0 != ret) {
      room.reset();
    }
    return room;
  }

  team_room::ptr_t setup_ready_room(int64_t team_id) { return setup_ready_room(make_team_key(team_id)); }

  // 创建队伍的完整流程: 就绪 -> create_team。返回 create_team 结果码；room 传出。
  // 就绪阶段复用 setup_ready_room(WAL 模式下事件批异步送达，需要泵空后重试一次)
  // client_version/user_router_server_id/shared_member_data 非缺省时随请求上报(与 lobbysvr create_team 的真实行为一致)
  int32_t setup_created_team(
      int64_t team_id, const PROJECT_NAMESPACE_ID::DUserIDKey& owner_key,
      const atfw::dtmq::DChannelIdKey& owner_channel, team_room::ptr_t* out_room = nullptr,
      const atfw::team::DTeamConfigure* configure = nullptr, const std::string* client_version = nullptr,
      uint64_t user_router_server_id = 0,
      const google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* shared_member_data = nullptr) {
    auto room = setup_ready_room(make_team_key(team_id));
    if (!room) {
      return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;
    }
    if (nullptr != out_room) {
      *out_room = room;
    }
    return run("setup_created_team",
               [room, team_id, &owner_key, &owner_channel, configure, client_version, user_router_server_id,
                shared_member_data](rpc::context& ctx) -> rpc::result_code_type {
                 atfw::team::SSTeamRoomCreateReq req;
                 protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
                 protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
                 protobuf_copy_message(*req.mutable_sender_user_channel(), owner_channel);
                 if (nullptr != configure) {
                   protobuf_copy_message(*req.mutable_configure(), *configure);
                 }
                 if (nullptr != client_version) {
                   req.set_client_version(*client_version);
                 }
                 if (0 != user_router_server_id) {
                   req.set_user_router_server_id(user_router_server_id);
                 }
                 if (nullptr != shared_member_data) {
                   req.mutable_shared_member_data()->CopyFrom(*shared_member_data);
                 }
                 RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
               });
  }

  // 把 journal 中未下发的新日志推送给订阅者，并执行与 task_action_channel_event_sync 相同的
  // manager flush。事件回调在泵循环中异步完成(与真实 dispatcher 时序一致)，推送后先泵几轮再 flush。
  // force_snapshot=true 时改推全量快照(恢复/重置场景)。
  // WAL journal 模式下事件由真实 wal_publisher vtable 主动下发，这里只负责收敛
  int32_t sync(const atfw::team::DTeamKey& team_key, bool force_snapshot = false) {
    if (wal_journal_mode) {
      (void)team_key;
      (void)force_snapshot;
      return wal_converge();
    }
    int32_t ret = run("sync_events", [this, &team_key, force_snapshot](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(push_channel_events(ctx, team_key, force_snapshot)));
    });
    if (0 != ret) {
      return ret;
    }
    return converge_events();
  }

  int32_t sync(int64_t team_id, bool force_snapshot = false) { return sync(make_team_key(team_id), force_snapshot); }

  // 推送调用方自行构造的 event_sync(EVT-10 故障批: 重复/乱序/hash 不匹配/旧 compact 点日志/中途坏 Any)，
  // 随后执行与 sync 相同的收敛。from_node_id 可切换来源节点(FLT-08)。
  int32_t sync_custom_event_sync(atfw::dtmq::SSChannelEventSync& event_sync, uint64_t from_node_id = kDtmqProxyNodeId) {
    if (event_sync.subscriber_keys().empty()) {
      event_sync.add_subscriber_keys(shared_subscriber_key_for());
    }
    int32_t ret = run("sync_custom", [&event_sync, from_node_id](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
          rpc::dtmq::client_subscriber::global_receive_channel_event(ctx, from_node_id, event_sync)));
    });
    if (0 != ret) {
      return ret;
    }
    return converge_events();
  }

  // 驱动一轮: 订阅者心跳 tick + 房间时间轮 tick；等待本轮实际启动的定时任务完成。
  // 返回值: 本轮触发的房间定时器数量(0 表示时间轮无到期事件)。
  int32_t drive_timer_ticks() {
    int32_t fired = 0;
    int32_t ret = run("drive_ticks", [&fired](rpc::context& ctx) -> rpc::result_code_type {
      rpc::dtmq::client_subscriber::global_tick(ctx);
      fired = team_room_manager::me()->tick(ctx);
      RPC_RETURN_CODE(0);
    });
    CASE_EXPECT_EQ(0, ret);
    if (0 != ret) {
      return std::numeric_limits<int32_t>::min();
    }
    bool completed = wait_until([]() { return !team_room_manager::me()->debug_has_running_maintenance_task(); });
    CASE_EXPECT_TRUE(completed);
    if (!completed) {
      return std::numeric_limits<int32_t>::min();
    }
    completed = wait_for_send_message_handlers();
    CASE_EXPECT_TRUE(completed);
    if (!completed) {
      return std::numeric_limits<int32_t>::min();
    }
    return fired;
  }

  // 用例结束清理: 房间与定时器。
  static void clear_rooms() {
    if (!team_room_manager::is_instance_destroyed()) {
      team_room_manager::me()->clear();
    }
  }

 private:
  template <class Predicate>
  bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      runtime_->pump_once();
      std::this_thread::yield();
    }
    return true;
  }

  bool wait_for_send_message_handlers() {
    // pump_once 的公开契约会依次收集 raw transport、投递 SS mock，再运行 dispatcher；这一轮把
    // 已提交的 no-wait 请求转换成 mock history，随后以 handler 实际进入次数等待完成。
    if (runtime_->pump_once() < 0) {
      return false;
    }
    size_t submitted = runtime_->ss().calls(rpc::dtmq::packer::get_full_name_of_send_message());
    return wait_until([this, submitted]() { return send_message_handler_attempts_ >= submitted; });
  }

  // global_receive_channel_event 在调用栈内完成快照/WAL 应用；flush 发起 no-wait 个人频道 RPC。
  // 以 mock history 与 handler 实际进入次数配对，超时只用于检测死锁，不参与业务正确性。
  int32_t converge_events() {
    int32_t ret = run("sync_flush", [](rpc::context& ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(team_room_manager::me()->flush_pending_channel_message(ctx)));
    });
    if (0 != ret) {
      return ret;
    }
    bool completed = wait_for_send_message_handlers();
    CASE_EXPECT_TRUE(completed);
    if (!completed) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    }
    return 0;
  }

 public:
  // 构造推送事件(incremental)或快照。协程内调用。
  rpc::result_code_type push_channel_events(rpc::context& ctx, const atfw::team::DTeamKey& team_key,
                                            bool force_snapshot) {
    auto& fake = channel(team_key);
    fake.ensure_created();

    if (force_snapshot || !fake.snapshot_pushed_) {
      atfw::dtmq::SSChannelEventSync event_sync;
      *event_sync.mutable_channel_snapshot() = fake.dump_snapshot();
      event_sync.add_subscriber_keys(shared_subscriber_key_for());
      auto ret = RPC_AWAIT_CODE_RESULT(
          rpc::dtmq::client_subscriber::global_receive_channel_event(ctx, kDtmqProxyNodeId, event_sync));
      if (0 == ret) {
        fake.mark_snapshot_pushed(fake.last_sequence_);
      }
      RPC_RETURN_CODE(ret);
    }

    if (!fake.has_pending_changes()) {
      RPC_RETURN_CODE(0);
    }

    atfw::dtmq::SSChannelEventSync event_sync;
    auto* metadata = event_sync.mutable_channel_metadata();
    metadata->mutable_channel_key()->CopyFrom(fake.channel_key_);
    metadata->set_last_sequence(fake.last_sequence_);
    metadata->set_last_hash_code(fake.last_hash_);
    metadata->set_create_sequence(fake.create_sequence_);
    protobuf_copy_message(*metadata->mutable_create_timepoint(), fake.create_timepoint_);
    if (!fake.custom_data_.type_url().empty()) {
      metadata->set_custom_data_sequence(fake.custom_data_sequence_);
      metadata->mutable_custom_data()->CopyFrom(fake.custom_data_);
    }
    auto* runtime_data = event_sync.mutable_channel_runtime();
    runtime_data->set_last_removed_sequence(fake.last_removed_sequence_);
    if (!fake.private_data_.type_url().empty()) {
      runtime_data->set_private_data_sequence(fake.private_data_sequence_);
      runtime_data->mutable_private_data()->CopyFrom(fake.private_data_);
    }
    if (fake.destroyed_) {
      metadata->set_destroy_sequence(fake.destroy_sequence_);
      protobuf_copy_message(*metadata->mutable_destroy_timepoint(), fake.destroy_timepoint_);
    }
    for (const auto& message : fake.journal_) {
      if (message.sequence() <= fake.last_pushed_sequence_) {
        continue;
      }
      *event_sync.add_channel_message() = message;
    }
    event_sync.add_subscriber_keys(shared_subscriber_key_for());
    auto ret = RPC_AWAIT_CODE_RESULT(
        rpc::dtmq::client_subscriber::global_receive_channel_event(ctx, kDtmqProxyNodeId, event_sync));
    if (0 == ret) {
      fake.mark_pushed(fake.last_sequence_);
    }
    RPC_RETURN_CODE(ret);
  }

  // 直接向 fake journal 注入一条 DTeamAction 事件日志(EVT/CMP 用例)。
  atfw::dtmq::DChannelMessage* inject_team_action(int64_t team_id, const atfw::team::DTeamAction& action,
                                                  int64_t forced_sequence_gap = 0) {
    auto& fake = channel(team_id);
    fake.ensure_created();
    return fake.append_log(
        [&action](atfw::dtmq::DChannelMessageDetail& detail) { (void)detail.mutable_event()->PackFrom(action); },
        forced_sequence_gap);
  }

  // 直接注入非 event 日志(EVT-11)。
  template <class Fn>
  atfw::dtmq::DChannelMessage* inject_raw_log(int64_t team_id, Fn&& detail_fill, int64_t forced_sequence_gap = 0) {
    auto& fake = channel(team_id);
    fake.ensure_created();
    return fake.append_log(std::forward<Fn>(detail_fill), forced_sequence_gap);
  }

 private:
  bool setup_discovery_nodes() {
    // dtmq-proxysvr 节点: 订阅心跳与所有写请求的一致性哈希目标
    {
      atframework::testing::mock_node node;
      node.set_id(kDtmqProxyNodeId)
          .set_name("unit-test-dtmq-proxy")
          .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtMqProxySvr))
          .set_type_name("dtmq-proxysvr")
          .set_zone_id(1)
          .add_label("hpa_scaling_ready", "1")
          .add_label("hpa_scaling_target", "1");
      if (!runtime_->discovery().add_node(node)) {
        return false;
      }
    }

    // 本地 teamsvr-room 节点: action 层一致性哈希路由命中本节点
    {
      atframework::testing::mock_node node;
      node.set_id(kLocalRoomNodeId)
          .set_name("unit-test-teamsvr-room")
          .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kTeamRoomSvr))
          .set_type_name("teamsvr-room")
          .set_zone_id(1)
          .add_label("hpa_scaling_ready", "1")
          .add_label("hpa_scaling_target", "1");
      if (!runtime_->discovery().add_node(node)) {
        return false;
      }
    }

    if (nullptr != logic_server_last_common_module()) {
      logic_server_last_common_module()->reload();
    }
    return true;
  }

  bool register_server_rules() {
    room_test_env* self = this;

    // subscribe: ack 心跳；首轮心跳在响应前推送就绪快照(模拟真实服务端的异步 snapshot 下发)。
    subscribe_rule_ = runtime_->ss().mock(
        rpc::dtmq::packer::get_full_name_of_subscribe(), atfw::dtmq::SSChannelSubscribeReq::descriptor()->full_name(),
        atfw::dtmq::SSChannelSubscribeRsp::descriptor()->full_name(),
        [self](const atframework::testing::ss_request_view& request,
               google::protobuf::Message& response) -> rpc::result_code_type {
          if (self->subscribe_fail_times_ > 0) {
            --self->subscribe_fail_times_;
            RPC_RETURN_CODE(self->subscribe_fail_code_);
          }
          const auto& typed_request = static_cast<const atfw::dtmq::SSChannelSubscribeReq&>(request.body);
          auto& typed_response = static_cast<atfw::dtmq::SSChannelSubscribeRsp&>(response);
          if (self->wal_journal_mode) {
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                self->wal_handle_subscribe(*request.context, typed_request, typed_response, request.target_node_id)));
          }
          for (const auto& heartbeat : typed_request.heartbeat()) {
            auto* fake = self->find_channel_by_key(heartbeat.channel_key());
            if (nullptr == fake) {
              continue;
            }
            fake->ensure_created();
            if (!fake->snapshot_pushed_ && nullptr != request.context) {
              RPC_AWAIT_IGNORE_RESULT(self->push_channel_events(*request.context, fake->team_key(), true));
            }
            auto* node = typed_response.add_subscribe_node();
            node->mutable_channel_key()->CopyFrom(heartbeat.channel_key());
            node->set_server_id(request.target_node_id);
            node->set_readonly_index(heartbeat.readonly_index());
          }
          RPC_RETURN_CODE(0);
        });
    if (!subscribe_rule_) {
      return false;
    }

    // send_message: team 房间频道走 journal + 锁语义；个人频道捕获 DTeamMemberAction。
    send_message_rule_ = runtime_->ss().mock(
        rpc::dtmq::packer::get_full_name_of_send_message(),
        atfw::dtmq::SSChannelSendMessageReq::descriptor()->full_name(),
        atfw::dtmq::SSChannelSendMessageRsp::descriptor()->full_name(),
        [self](const atframework::testing::ss_request_view& request,
               google::protobuf::Message& response) -> rpc::result_code_type {
          ++self->send_message_handler_attempts_;
          const auto& typed_request = static_cast<const atfw::dtmq::SSChannelSendMessageReq&>(request.body);
          auto& typed_response = static_cast<atfw::dtmq::SSChannelSendMessageRsp&>(response);

          fake_team_room_channel* fake = nullptr;
          if (self->wal_journal_mode) {
            // WAL 模式: team-room 频道走真实 mq_channel;其他频道类型继续走个人频道捕获
            if (typed_request.channel_key().channel_type() == kTeamRoomChannelType) {
              if (nullptr == request.context) {
                RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
              }
              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
                  self->wal_handle_send_message(*request.context, typed_request, typed_response)));
            }
          } else {
            fake = self->find_channel_by_key(typed_request.channel_key());
          }
          if (nullptr == fake) {
            // 个人频道发送使用 no-wait stream，没有业务响应。这里编排的是远端处理结果:
            // commit_first=false 表示处理前丢弃；commit_first=true 表示先接收消息、随后处理失败。
            if (!self->personal_send_plan_.empty()) {
              auto plan = self->personal_send_plan_.front();
              self->personal_send_plan_.pop_front();
              if (!plan.first) {
                RPC_RETURN_CODE(plan.second);
              }
              for (const auto& message_content : typed_request.message_content()) {
                personal_message_record record;
                record.channel.CopyFrom(typed_request.channel_key());
                if (message_content.detail().has_event()) {
                  (void)message_content.detail().event().UnpackTo(&record.action);
                }
                self->personal_messages_.push_back(std::move(record));
              }
              if (plan.second != 0) {
                RPC_RETURN_CODE(plan.second);
              }
              typed_response.set_client_result(0);
              RPC_RETURN_CODE(0);
            }
            for (const auto& message_content : typed_request.message_content()) {
              personal_message_record record;
              record.channel.CopyFrom(typed_request.channel_key());
              if (message_content.detail().has_event()) {
                (void)message_content.detail().event().UnpackTo(&record.action);
              }
              self->personal_messages_.push_back(std::move(record));
            }
            typed_response.set_client_result(0);
            RPC_RETURN_CODE(0);
          }

          RPC_RETURN_CODE(
              RPC_AWAIT_CODE_RESULT(self->fake_handle_send_message(typed_request, typed_response, request.context)));
        });
    if (!send_message_rule_) {
      return false;
    }

    // update: 保存 custom/private + 压缩边界 + 锁续租
    update_rule_ = runtime_->ss().mock(
        rpc::dtmq::packer::get_full_name_of_update(), atfw::dtmq::SSChannelUpdateReq::descriptor()->full_name(),
        atfw::dtmq::SSChannelUpdateRsp::descriptor()->full_name(),
        [self](const atframework::testing::ss_request_view& request,
               google::protobuf::Message& response) -> rpc::result_code_type {
          const auto& typed_request = static_cast<const atfw::dtmq::SSChannelUpdateReq&>(request.body);
          auto& typed_response = static_cast<atfw::dtmq::SSChannelUpdateRsp&>(response);

          if (self->wal_journal_mode) {
            if (typed_request.channel_key().channel_type() == kTeamRoomChannelType) {
              if (nullptr == request.context) {
                RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
              }
              RPC_RETURN_CODE(
                  RPC_AWAIT_CODE_RESULT(self->wal_handle_update(*request.context, typed_request, typed_response)));
            }
            typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
            RPC_RETURN_CODE(0);
          }

          auto* fake = self->find_channel_by_key(typed_request.channel_key());
          if (nullptr == fake) {
            typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
            RPC_RETURN_CODE(0);
          }

          RPC_RETURN_CODE(
              RPC_AWAIT_CODE_RESULT(self->fake_handle_update(typed_request, typed_response, request.context)));
        });
    if (!update_rule_) {
      return false;
    }

    // reset_lock: 锁 CAS
    reset_lock_rule_ = runtime_->ss().mock(
        rpc::dtmq::packer::get_full_name_of_reset_lock(), atfw::dtmq::SSChannelResetLockReq::descriptor()->full_name(),
        atfw::dtmq::SSChannelResetLockRsp::descriptor()->full_name(),
        [self](const atframework::testing::ss_request_view& request,
               google::protobuf::Message& response) -> rpc::result_code_type {
          const auto& typed_request = static_cast<const atfw::dtmq::SSChannelResetLockReq&>(request.body);
          auto& typed_response = static_cast<atfw::dtmq::SSChannelResetLockRsp&>(response);

          if (self->wal_journal_mode) {
            if (typed_request.channel_key().channel_type() == kTeamRoomChannelType) {
              if (nullptr == request.context) {
                RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
              }
              RPC_RETURN_CODE(
                  RPC_AWAIT_CODE_RESULT(self->wal_handle_reset_lock(*request.context, typed_request, typed_response)));
            }
            typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
            RPC_RETURN_CODE(0);
          }

          auto* fake = self->find_channel_by_key(typed_request.channel_key());
          if (nullptr == fake) {
            typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
            RPC_RETURN_CODE(0);
          }

          RPC_RETURN_CODE(
              RPC_AWAIT_CODE_RESULT(self->fake_handle_reset_lock(typed_request, typed_response, request.context)));
        });
    if (!reset_lock_rule_) {
      return false;
    }

    // destroy_channel: 销毁频道
    destroy_rule_ = runtime_->ss().mock(
        rpc::dtmq::packer::get_full_name_of_destroy_channel(),
        atfw::dtmq::SSChannelDestroyChannelReq::descriptor()->full_name(),
        google::protobuf::Empty::descriptor()->full_name(),
        [self](const atframework::testing::ss_request_view& request,
               google::protobuf::Message& /*response*/) -> rpc::result_code_type {
          const auto& typed_request = static_cast<const atfw::dtmq::SSChannelDestroyChannelReq&>(request.body);

          if (self->wal_journal_mode) {
            if (typed_request.channel_key().channel_type() == kTeamRoomChannelType) {
              if (nullptr == request.context) {
                RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
              }
              RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(self->wal_handle_destroy(*request.context, typed_request)));
            }
            RPC_RETURN_CODE(0);
          }

          auto* fake = self->find_channel_by_key(typed_request.channel_key());
          if (nullptr == fake) {
            RPC_RETURN_CODE(0);
          }

          fake->ensure_created();
          if (typed_request.has_compare_and_maybe_reset_lock()) {
            atfw::dtmq::channel_lock_checker checker = typed_request.compare_and_maybe_reset_lock();
            if (!fake->check_lock(checker)) {
              RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
            }
          }
          auto fault = fake->take_destroy_fault();
          if (fault.present && fault.commit_first) {
            fake->apply_destroy();
          }
          if (fault.present) {
            RPC_RETURN_CODE(fault.error_code);
          }

          fake->apply_destroy();
          RPC_RETURN_CODE(0);
        });
    return !!destroy_rule_;
  }

  // ---- fake 层服务端逻辑(默认规则与 preempt 故障规则共用; WAL 模式分支不经过这里) ----

  rpc::result_code_type fake_handle_send_message(const atfw::dtmq::SSChannelSendMessageReq& typed_request,
                                                 atfw::dtmq::SSChannelSendMessageRsp& typed_response,
                                                 rpc::context* ctx) {
    auto* fake = find_channel_by_key(typed_request.channel_key());
    if (nullptr == fake) {
      // 调用方负责区分个人频道; preempt 路径只面向 team-room 频道,兜底按无效频道处理
      typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
      RPC_RETURN_CODE(0);
    }

    ++fake->send_message_calls_;
    auto fault = fake->take_send_fault();
    if (fault.present && !fault.commit_first) {
      // 预提交失败: 锁/journal/快照均不变(请求到达前失败语义)
      RPC_RETURN_CODE(fault.error_code);
    }
    fake->ensure_created();
    if (typed_request.has_compare_and_maybe_reset_lock()) {
      atfw::dtmq::channel_lock_checker checker = typed_request.compare_and_maybe_reset_lock();
      if (!fake->check_lock(checker)) {
        protobuf_copy_message(*typed_response.mutable_compare_and_maybe_reset_lock(), checker);
        typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
        typed_response.set_last_sequence(fake->last_sequence());
        typed_response.set_last_hash_code(fake->last_hash());
        RPC_RETURN_CODE(0);
      }
    }

    if (fault.present /* && fault.commit_first */) {
      // 已提交但响应丢失: 先落 journal 再以错误响应
      for (const auto& message_content : typed_request.message_content()) {
        (void)fake->append_log([&message_content](atfw::dtmq::DChannelMessageDetail& detail) {
          protobuf_copy_message(detail, message_content.detail());
        });
      }
      RPC_RETURN_CODE(fault.error_code);
    }

    for (const auto& message_content : typed_request.message_content()) {
      auto* message = fake->append_log([&message_content](atfw::dtmq::DChannelMessageDetail& detail) {
        protobuf_copy_message(detail, message_content.detail());
      });
      typed_response.add_message_sequence(message->sequence());
    }
    typed_response.set_client_result(0);
    typed_response.set_last_sequence(fake->last_sequence());
    typed_response.set_last_hash_code(fake->last_hash());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(await_response_gate(ctx, send_message_response_gate)));
    RPC_RETURN_CODE(0);
  }

  rpc::result_code_type fake_handle_update(const atfw::dtmq::SSChannelUpdateReq& typed_request,
                                           atfw::dtmq::SSChannelUpdateRsp& typed_response, rpc::context* ctx) {
    auto* fake = find_channel_by_key(typed_request.channel_key());
    if (nullptr == fake) {
      typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
      RPC_RETURN_CODE(0);
    }

    auto fault = fake->take_update_fault();
    if (fault.present && !fault.commit_first) {
      // 预提交失败: 锁/custom/private/journal 均不变
      RPC_RETURN_CODE(fault.error_code);
    }
    fake->ensure_created();
    if (typed_request.has_compare_and_maybe_reset_lock()) {
      atfw::dtmq::channel_lock_checker checker = typed_request.compare_and_maybe_reset_lock();
      if (!fake->check_lock(checker)) {
        protobuf_copy_message(*typed_response.mutable_compare_and_maybe_reset_lock(), checker);
        typed_response.set_last_sequence(fake->last_sequence());
        typed_response.set_last_hash_code(fake->last_hash());
        typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
        RPC_RETURN_CODE(0);
      }
    }

    if (fault.present /* && fault.commit_first */) {
      // 已提交但响应丢失
      fake->apply_update(typed_request);
      RPC_RETURN_CODE(fault.error_code);
    }

    fake->apply_update(typed_request);
    typed_response.set_client_result(0);
    typed_response.set_last_sequence(fake->last_sequence());
    typed_response.set_last_hash_code(fake->last_hash());
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(await_response_gate(ctx, update_response_gate)));
    RPC_RETURN_CODE(0);
  }

  rpc::result_code_type fake_handle_reset_lock(const atfw::dtmq::SSChannelResetLockReq& typed_request,
                                               atfw::dtmq::SSChannelResetLockRsp& typed_response, rpc::context* ctx) {
    auto* fake = find_channel_by_key(typed_request.channel_key());
    if (nullptr == fake) {
      typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
      RPC_RETURN_CODE(0);
    }

    ++fake->reset_lock_calls_;
    auto fault = fake->take_reset_lock_fault();
    if (fault.present && !fault.commit_first) {
      // 预提交失败: 锁与 journal 均不变
      RPC_RETURN_CODE(fault.error_code);
    }
    fake->ensure_created();
    if (typed_request.has_compare_and_maybe_reset_lock()) {
      atfw::dtmq::channel_lock_checker checker = typed_request.compare_and_maybe_reset_lock();
      if (!fake->check_lock(checker)) {
        protobuf_copy_message(*typed_response.mutable_compare_and_maybe_reset_lock(), checker);
        typed_response.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
        RPC_RETURN_CODE(0);
      }
      protobuf_copy_message(*typed_response.mutable_compare_and_maybe_reset_lock(), checker);
    }

    if (fault.present /* && fault.commit_first */) {
      // 锁已重置但响应丢失
      RPC_RETURN_CODE(fault.error_code);
    }

    typed_response.set_client_result(0);
    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(await_response_gate(ctx, reset_lock_response_gate)));
    RPC_RETURN_CODE(0);
  }

  // 响应挂起门的 handler 侧实现: 挂起发生在提交与响应填充之后,响应内容冻结在提交时刻。
  // 虚拟时间前进不会在挂起期间发生(用例约定),因此 await 超时只是兜底。
  rpc::result_code_type await_response_gate(rpc::context* ctx, response_gate_t& gate) {
    if (!gate.armed) {
      RPC_RETURN_CODE(0);
    }
    gate.armed = false;  // 一次性消费
    if (nullptr == ctx) {
      CASE_MSG_INFO() << "response gate armed but the mock handler has no coroutine context\n";
      RPC_RETURN_CODE(0);
    }
    auto await_options = dispatcher_make_default<dispatcher_await_options>();
    await_options.sequence = ++gate_sequence_allocator_;
    await_options.timeout = std::chrono::seconds{30};
    gate.sequence = await_options.sequence;
    gate.parked_task = ctx->get_task_context().task_id;
    RPC_AWAIT_IGNORE_RESULT(rpc::custom_wait(*ctx, &gate.token, await_options));
    gate.parked_task = 0;
    RPC_RETURN_CODE(0);
  }

  using preempt_handler_t = std::function<rpc::result_code_type(
      room_test_env*, const atframework::testing::ss_request_view&, google::protobuf::Message&)>;

  atframework::testing::ss_rule_handle inject_response_fault_once(gsl::string_view full_rpc_name,
                                                                  gsl::string_view request_type_name,
                                                                  gsl::string_view response_type_name,
                                                                  response_fault_kind kind, preempt_handler_t handler) {
    atframework::testing::ss_rule_options options;
    options.preempt = true;
    options.times = 1;
    options.malformed_type_url = (response_fault_kind::kMalformedTypeUrl == kind);
    options.malformed_body = (response_fault_kind::kMalformedBody == kind);
    options.no_response = (response_fault_kind::kNoResponse == kind);
    room_test_env* self = this;
    return runtime_->ss().mock(
        full_rpc_name, request_type_name, response_type_name,
        [self, handler = std::move(handler)](const atframework::testing::ss_request_view& request,
                                             google::protobuf::Message& response) -> rpc::result_code_type {
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handler(self, request, response)));
        },
        options);
  }

  // ---- WAL journal mode 实现(镜像 task_action_* 的服务端行为，journal 换成真实 mq_channel) ----

  bool setup_wal_journal() {
    // manager 单例进程级初始化(一次): 注册时间轮与发现回调。本进程保持 teamsvr_room 的
    // server-instance config，mq_channel 内部读取 dtmq_proxysvr_cfg 默认实例(见文件头注释)
    static const int manager_init_result = mq_channel_manager::me()->init();
    if (0 != manager_init_result) {
      CASE_MSG_INFO() << "mq_channel_manager init failed: " << manager_init_result << '\n';
      return false;
    }

    // memory_only 频道不会触达 DB;注册消息类型仅保证任何意外路径进入 mock DB 而非崩溃
    runtime_->db().register_message_type<PROJECT_NAMESPACE_ID::table_dtmq_channel_record>();

    room_test_env* self = this;
    // 真实 vtable(publisher_send_snapshot/publisher_send_logs)发出的 channel_event_sync 在此
    // 转发给真实 client_subscriber，等价于 room 侧 task_action_channel_event_sync 的接线
    wal_event_sync_rule_ =
        rpc::dtmq::mock::channel_event_sync([self](rpc::context& ctx, const atfw::dtmq::SSChannelEventSync& request,
                                                   google::protobuf::Empty& /*response*/) -> rpc::result_code_type {
          ++self->wal_event_sync_handler_attempts_;
          self->wal_event_batches_.push_back(wal_event_sync_record{request});
          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
              rpc::dtmq::client_subscriber::global_receive_channel_event(ctx, kDtmqProxyNodeId, request)));
        });
    return !!wal_event_sync_rule_;
  }

  void wal_cleanup_for_case() {
    wal_channels_.clear();
    wal_event_batches_.clear();
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
    if (!mq_channel_manager::is_instance_destroyed()) {
      MqChannelManagerUnitTest::clear_all_channels(*mq_channel_manager::me());
    }
#endif
  }

  wal_channel_ptr_t wal_find_channel_by_key(const atfw::dtmq::DChannelIdKey& channel_key) const {
    auto iter = wal_channels_.find(channel_key.channel_id());
    return iter != wal_channels_.end() ? iter->second : nullptr;
  }

  rpc::result_code_type wal_ensure_channel_by_key(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key) {
    auto channel = wal_find_channel_by_key(channel_key);
    if (!channel) {
      auto configure = excel::get_dtmq_channel_configure(channel_key.channel_type());
      if (!configure) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_INVALID_CHANNEL);
      }
      channel =
          atfw::component::memory::stl::make_strong_rc<mq_channel>(*mq_channel_manager::me(), channel_key, *configure);
      if (!channel) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
      }
      mq_channel_manager::me()->add_channel(ctx, channel);
      wal_channels_.emplace(channel_key.channel_id(), channel);
    }

    if (!channel->is_available()) {
      if (!channel->is_writable()) {
        auto init_ret = RPC_AWAIT_CODE_RESULT(channel->writable_init(ctx, true));
        if (init_ret < 0) {
          RPC_RETURN_CODE(init_ret);
        }
      }
      if (!channel->is_available()) {
        // writable 主节点自行分配 create_timepoint/sequence(向 journal 追加 kCreate 日志)
        channel->set_created(ctx, atfw::util::time::time_utility::now(), 0);
      }
    }
    RPC_RETURN_CODE(0);
  }

  // WAL 模式的订阅者地址改写: 真实 vtable 会向 subscriber_server_id 发送 channel_event_sync。
  // 本进程内 room 节点就是本地 app id，atapp 的自发自送走 self endpoint 本地环回，不经过
  // mock 引擎的出站拦截。把订阅者地址改写到已注入的远程 dtmq-proxy 节点，事件下发就会走
  // mock SS 出站路径，由 channel_event_sync 规则转投给真实 client_subscriber。
  // 订阅者 key(路由/幂等标识)保持不变，仅改变事件下发目标地址
  static atfw::dtmq::channel_subscriber wal_redirect_subscriber(const atfw::dtmq::channel_subscriber& subscriber) {
    atfw::dtmq::channel_subscriber ret = subscriber;
    ret.set_subscriber_server_id(kDtmqProxyNodeId);
    return ret;
  }

  // 镜像 task_action_subscribe: 逐心跳建频道/合并订阅者并 tick;分布归属与转发由
  // component-dtmq-proxysvr 测试覆盖，这里对直连频道执行
  rpc::result_code_type wal_handle_subscribe(rpc::context& ctx, const atfw::dtmq::SSChannelSubscribeReq& req,
                                             atfw::dtmq::SSChannelSubscribeRsp& rsp, uint64_t target_node_id) {
    for (const auto& heartbeat : req.heartbeat()) {
      // fixture 范围裁剪: 本环境只承载 team-room 频道，其它类型按不存在处理
      // (生产 task_action_subscribe 无类型过滤，分发/转发语义由 component-dtmq-proxysvr 测试覆盖)
      if (heartbeat.channel_key().channel_type() != kTeamRoomChannelType) {
        rsp.add_not_found_channel_ids(heartbeat.channel_key().channel_id());
        continue;
      }

      auto channel = wal_find_channel_by_key(heartbeat.channel_key());
      if (!channel && heartbeat.auto_create_channel()) {
        auto ensure_ret = RPC_AWAIT_CODE_RESULT(wal_ensure_channel_by_key(ctx, heartbeat.channel_key()));
        if (ensure_ret < 0) {
          // 与 task_action_subscribe 一致: auto_create 建频道失败即终止整个 RPC 并回错误码
          // (只有非 auto_create 的 NOT_FOUND 才记入 not_found_channel_ids 继续批次)
          RPC_RETURN_CODE(ensure_ret);
        }
        channel = wal_find_channel_by_key(heartbeat.channel_key());
      }
      if (!channel || (!channel->is_readonly() && !channel->is_writable())) {
        rsp.add_not_found_channel_ids(heartbeat.channel_key().channel_id());
        continue;
      }

      if (req.has_subscriber() && req.subscriber().subscriber_server_id() != 0) {
        channel->subscribe(ctx, wal_redirect_subscriber(req.subscriber()), heartbeat.last_sequence(),
                           heartbeat.last_hash_code(), false);
        auto* node = rsp.add_subscribe_node();
        node->mutable_channel_key()->CopyFrom(heartbeat.channel_key());
        node->set_server_id(target_node_id);
        node->set_readonly_index(heartbeat.readonly_index());
      }

      channel->tick(ctx);
    }
    RPC_RETURN_CODE(0);
  }

  // 镜像 task_action_send_message
  rpc::result_code_type wal_handle_send_message(rpc::context& ctx, const atfw::dtmq::SSChannelSendMessageReq& req,
                                                atfw::dtmq::SSChannelSendMessageRsp& rsp) {
    auto channel = wal_find_channel_by_key(req.channel_key());
    if (!channel || !channel->is_available()) {
      rsp.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
      RPC_RETURN_CODE(0);
    }

    if (req.has_compare_and_maybe_reset_lock()) {
      atfw::dtmq::channel_lock_checker checker = req.compare_and_maybe_reset_lock();
      if (!channel->compare_and_maybe_reset_lock(ctx, checker, true)) {
        protobuf_copy_message(*rsp.mutable_compare_and_maybe_reset_lock(), checker);
        rsp.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
        rsp.set_last_sequence(channel->get_last_message_sequence());
        rsp.set_last_hash_code(channel->get_last_hash_code());
        RPC_RETURN_CODE(0);
      }
    }

    int32_t result = 0;
    mq_channel_wal_object_context param{ctx, result};

    // 正常发送接口只允许追加(sequence=0 -> allocate_log_key 分配真实递增序号)
    rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    for (const auto& request_message : req.message_content()) {
      atfw::dtmq::DChannelMessage content = request_message;
      content.set_sequence(0);
      auto message = channel->get_wal_publisher().allocate_log(
          atfw::util::time::time_utility::now(), content.detail().command_case(), param, std::move(content));
      if (message) {
        message->set_channel_type(req.channel_key().channel_type());
        rsp.add_message_sequence(message->sequence());
        channel->get_wal_publisher().emplace_back_log(std::move(message), param);
      } else {
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        result = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
        break;
      }
    }
    if (req.message_content().empty()) {
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      result = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
    } else {
      channel->tick(ctx);
    }

    // 与 task_action_send_message 保持一致: 发送接口不再自动订阅
    // (client_subscriber::send_message 携带的是发送方自己的 subscriber_key，底层复用 shared_subscriber，
    // 自动订阅会创建冗余订阅项；仅发送不订阅的客户端也不应被隐式订阅)

    rsp.set_client_result(result);
    rsp.set_last_sequence(channel->get_last_message_sequence());
    rsp.set_last_hash_code(channel->get_last_hash_code());
    RPC_RETURN_CODE(ret);
  }

  // 镜像 task_action_update
  rpc::result_code_type wal_handle_update(rpc::context& ctx, const atfw::dtmq::SSChannelUpdateReq& req,
                                          atfw::dtmq::SSChannelUpdateRsp& rsp) {
    auto channel = wal_find_channel_by_key(req.channel_key());
    if (!channel || !channel->is_available()) {
      rsp.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
      RPC_RETURN_CODE(0);
    }

    if (req.has_compare_and_maybe_reset_lock()) {
      atfw::dtmq::channel_lock_checker checker = req.compare_and_maybe_reset_lock();
      if (!channel->compare_and_maybe_reset_lock(ctx, checker, true)) {
        protobuf_copy_message(*rsp.mutable_compare_and_maybe_reset_lock(), checker);
        rsp.set_last_sequence(channel->get_last_message_sequence());
        rsp.set_last_hash_code(channel->get_last_hash_code());
        rsp.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
        RPC_RETURN_CODE(0);
      }
    }

    bool has_changed_custom_data = false;
    if (req.clear_custom_data_action()) {
      has_changed_custom_data = channel->clear_custom_data();
    } else if (req.has_custom_data()) {
      has_changed_custom_data = channel->set_custom_data(req.custom_data());
    }

    bool has_changed_private_data = false;
    if (req.clear_private_data_action()) {
      has_changed_private_data = channel->clear_private_data();
    } else if (req.has_private_data()) {
      has_changed_private_data = channel->set_private_data(req.private_data());
    }

    int32_t result = 0;
    if (has_changed_custom_data && !req.custom_data_skip_notify()) {
      mq_channel_wal_object_context param{ctx, result};
      auto message = channel->get_wal_publisher().allocate_log(
          atfw::util::time::time_utility::now(), atfw::dtmq::DChannelMessageDetail::kUpdateCustomData, param);
      if (message) {
        message->mutable_detail()->set_update_custom_data(true);
        if (req.has_subscriber()) {
          message->set_sender_key(req.subscriber().subscriber_key());
        }
        channel->get_wal_publisher().emplace_back_log(std::move(message), param);
        channel->reset_custom_data_sequence();
      }
    } else if (has_changed_private_data || has_changed_custom_data) {
      mq_channel_wal_object_context param{ctx, result};
      auto message = channel->get_wal_publisher().allocate_log(atfw::util::time::time_utility::now(),
                                                               atfw::dtmq::DChannelMessageDetail::kNoop, param);
      if (message) {
        message->mutable_detail()->set_noop(true);
        if (req.has_subscriber()) {
          message->set_sender_key(req.subscriber().subscriber_key());
        }
        channel->get_wal_publisher().emplace_back_log(std::move(message), param);
      }
    }

    if (has_changed_private_data) {
      channel->reset_private_data_sequence();
    }

    // wal_object::remove_before 以 back().timepoint < now(严格小于)为移除门禁。本 handler 刚
    // 追加的通知日志与压缩调用在冻结的虚拟时钟下可能落在同一微秒，物理裁剪会被跳过；生产环境
    // 两次调用之间真实时间总是推进的(否则下一轮维护重试，见 pick_compact_sequence 的重选语义)，
    // 这里推进 1ms 做等价模拟
    if (req.compact_sequence() > 0) {
      atfw::util::time::time_utility::set_global_now_offset(atfw::util::time::time_utility::get_global_now_offset() +
                                                            std::chrono::milliseconds{1});
    }
    channel->compact_stateful_sequence(req.stateful_sequence());
    channel->compact_sequence(req.compact_sequence());

    channel->tick(ctx);

    if (req.has_subscriber()) {
      if (!req.subscriber().subscriber_key().empty() && req.subscriber().last_heartbeat_timepoint().seconds() > 0) {
        channel->subscribe(ctx, wal_redirect_subscriber(req.subscriber()), req.subscriber().last_heartbeat_sequence(),
                           req.subscriber().last_heartbeat_hash_code(), true);
      }
    }
    for (const auto& other_subscriber : req.update_others()) {
      if (other_subscriber.last_heartbeat_timepoint().seconds() > 0) {
        channel->subscribe(ctx, wal_redirect_subscriber(other_subscriber), other_subscriber.last_heartbeat_sequence(),
                           other_subscriber.last_heartbeat_hash_code(), true);
      }
    }

    if (req.save()) {
      result = RPC_AWAIT_CODE_RESULT(channel->await_io_task(ctx));
      if (result < 0) {
        result = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_UPDATED_BUT_SAVE_FAILED;
      } else {
        int32_t save_io_result = channel->async_save(ctx);
        if (save_io_result < 0) {
          result = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_UPDATED_BUT_SAVE_FAILED;
        } else {
          int32_t io_result = 0;
          result = RPC_AWAIT_CODE_RESULT(channel->await_io_task(ctx, &io_result));
          if (result >= 0 && io_result < 0) {
            result = io_result;
          }
          if (result < 0) {
            result = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_UPDATED_BUT_SAVE_FAILED;
          }
        }
      }
    }

    rsp.set_client_result(result);
    rsp.set_last_sequence(channel->get_last_message_sequence());
    rsp.set_last_hash_code(channel->get_last_hash_code());
    RPC_RETURN_CODE(0);
  }

  // 镜像 task_action_reset_lock
  rpc::result_code_type wal_handle_reset_lock(rpc::context& ctx, const atfw::dtmq::SSChannelResetLockReq& req,
                                              atfw::dtmq::SSChannelResetLockRsp& rsp) {
    auto channel = wal_find_channel_by_key(req.channel_key());
    if (!channel || !channel->is_available()) {
      rsp.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_NOT_FOUND);
      RPC_RETURN_CODE(0);
    }

    if (req.has_compare_and_maybe_reset_lock()) {
      atfw::dtmq::channel_lock_checker checker = req.compare_and_maybe_reset_lock();
      if (!channel->compare_and_maybe_reset_lock(ctx, checker, true)) {
        protobuf_copy_message(*rsp.mutable_compare_and_maybe_reset_lock(), checker);
        rsp.set_client_result(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
        RPC_RETURN_CODE(0);
      }
      protobuf_copy_message(*rsp.mutable_compare_and_maybe_reset_lock(), checker);
    }

    channel->tick(ctx);
    rsp.set_client_result(0);
    RPC_RETURN_CODE(0);
  }

  // 镜像 task_action_destroy_channel
  rpc::result_code_type wal_handle_destroy(rpc::context& ctx, const atfw::dtmq::SSChannelDestroyChannelReq& req) {
    auto channel = wal_find_channel_by_key(req.channel_key());
    // destroy 不存在的频道视为成功
    if (!channel || !channel->is_available()) {
      RPC_RETURN_CODE(0);
    }

    channel->tick(ctx);

    if (req.has_compare_and_maybe_reset_lock()) {
      atfw::dtmq::channel_lock_checker checker = req.compare_and_maybe_reset_lock();
      if (!channel->compare_and_maybe_reset_lock(ctx, checker, true)) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
      }
    }

    if (channel->is_destroyed()) {
      RPC_RETURN_CODE(0);
    }

    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(channel->destroy(ctx, std::chrono::system_clock::from_time_t(0), 0)));
  }

 private:
  room_test_cfg_values cfg_;
  std::unique_ptr<atframework::testing::runtime> runtime_;
  std::unordered_map<atfw::team::DTeamKey, std::shared_ptr<fake_team_room_channel>,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      channels_;
  std::vector<personal_message_record> personal_messages_;
  size_t send_message_handler_attempts_ = 0;
  uint64_t gate_sequence_allocator_ = 0;
  std::deque<std::pair<bool, int32_t>> personal_send_plan_;
  uint32_t subscribe_fail_times_ = 0;
  int32_t subscribe_fail_code_ = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;

  atframework::testing::ss_rule_handle subscribe_rule_;
  atframework::testing::ss_rule_handle send_message_rule_;
  atframework::testing::ss_rule_handle update_rule_;
  atframework::testing::ss_rule_handle reset_lock_rule_;
  atframework::testing::ss_rule_handle destroy_rule_;

  // ---- WAL journal mode 状态 ----
  std::unordered_map<std::string, wal_channel_ptr_t> wal_channels_;
  std::vector<wal_event_sync_record> wal_event_batches_;
  size_t wal_event_sync_handler_attempts_ = 0;
  rpc::unit_test::mock_rule_handle wal_event_sync_rule_;
};

// ---- 用例内常用构造器 ----

inline PROJECT_NAMESPACE_ID::DUserIDKey make_user_key(uint32_t zone_id, uint64_t user_id) {
  PROJECT_NAMESPACE_ID::DUserIDKey key;
  key.set_zone_id(zone_id);
  key.set_user_id(user_id);
  return key;
}

inline atfw::dtmq::DChannelIdKey make_personal_channel(uint64_t user_id) {
  atfw::dtmq::DChannelIdKey key;
  key.set_channel_type(1);  // 个人通知频道类型(与 team room 频道区分)
  key.set_channel_id("ut-user-" + std::to_string(user_id));
  return key;
}

// 本节点作为房间主控时的乐观锁持有者标识(与 team_room::create 的 lock_holder 推导一致)
inline std::string self_lock_holder() {
  gsl::string_view server_name = logic_config::me()->get_local_server_name();
  if (server_name.empty()) {
    return atfw::util::string::format("teamsvr-room:{:#x}", logic_config::me()->get_local_server_id());
  }
  return atfw::util::string::format("teamsvr-room:{}", server_name);
}

// 统计发往指定用户个人频道的某类 DTeamMemberAction 数量
inline size_t count_personal_actions(const room_test_env& env, uint64_t user_id,
                                     atfw::team::DTeamMemberAction::ActionCase action_case) {
  size_t ret = 0;
  const std::string channel_id = make_personal_channel(user_id).channel_id();
  for (const auto& record : env.personal_messages()) {
    if (record.channel.channel_id() == channel_id && record.action.action_case() == action_case) {
      ++ret;
    }
  }
  return ret;
}

// 构造一条他人持有的乐观锁(expire_after_seconds 秒后过期；<=0 表示已过期)
inline atfw::dtmq::DChannelOptimisticLock make_foreign_lock(gsl::string_view holder, int64_t expire_after_seconds) {
  atfw::dtmq::DChannelOptimisticLock ret;
  ret.set_lock_holder(std::string(holder));
  *ret.mutable_timeout() =
      protobuf_from_system_clock(atfw::util::time::time_utility::now() + std::chrono::seconds{expire_after_seconds});
  return ret;
}

// 标准测试队伍: owner(OWNER) + admin(ADMIN) + normal(NORMAL) + outsider(非成员)。
// 构造流程走真实写路径: create_team -> add_member(admin) -> add_member(normal) -> 事件回环。
struct standard_team_members {
  PROJECT_NAMESPACE_ID::DUserIDKey owner;
  PROJECT_NAMESPACE_ID::DUserIDKey admin;
  PROJECT_NAMESPACE_ID::DUserIDKey normal;
  PROJECT_NAMESPACE_ID::DUserIDKey outsider;
  atfw::dtmq::DChannelIdKey owner_channel;
  atfw::dtmq::DChannelIdKey admin_channel;
  atfw::dtmq::DChannelIdKey normal_channel;
};

inline bool setup_standard_team(room_test_env& env, int64_t team_id, team_room::ptr_t& out_room,
                                standard_team_members& members, uint64_t user_id_base = 7000) {
  members.owner = make_user_key(1, user_id_base + 1);
  members.admin = make_user_key(1, user_id_base + 2);
  members.normal = make_user_key(1, user_id_base + 3);
  members.outsider = make_user_key(1, user_id_base + 4);
  members.owner_channel = make_personal_channel(user_id_base + 1);
  members.admin_channel = make_personal_channel(user_id_base + 2);
  members.normal_channel = make_personal_channel(user_id_base + 3);

  if (0 != env.setup_created_team(team_id, members.owner, members.owner_channel, &out_room)) {
    return false;
  }
  if (0 != env.sync(team_id)) {
    return false;
  }

  // owner 添加 admin/normal 成员
  return 0 == env.run("setup_members",
                      [&members, &out_room](rpc::context& ctx) -> rpc::result_code_type {
                        {
                          atfw::team::DTeamAction action;
                          auto* add_member = action.mutable_add_member();
                          protobuf_copy_message(*add_member->mutable_user_key(), members.admin);
                          protobuf_copy_message(*add_member->mutable_user_channel(), members.admin_channel);
                          add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
                          int32_t ret = RPC_AWAIT_CODE_RESULT(out_room->send_action(ctx, action));
                          if (0 != ret) {
                            RPC_RETURN_CODE(ret);
                          }
                        }
                        {
                          atfw::team::DTeamAction action;
                          auto* add_member = action.mutable_add_member();
                          protobuf_copy_message(*add_member->mutable_user_key(), members.normal);
                          protobuf_copy_message(*add_member->mutable_user_channel(), members.normal_channel);
                          add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
                          RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(out_room->send_action(ctx, action)));
                        }
                      }) &&
         0 == env.sync(team_id) && nullptr != out_room->find_member(members.admin, false) &&
         nullptr != out_room->find_member(members.normal, false);
}

}  // namespace teamsvr_room_test
