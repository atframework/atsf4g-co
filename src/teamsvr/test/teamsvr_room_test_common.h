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
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <atframe/atapp.h>
#include "atframe/atapp_conf.h"  // IWYU pragma: keep
#include "config/extern_service_types.h"
#include "config/logic_config.h"
#include "frame/test_macros.h"  // IWYU pragma: keep
#include "logic/logic_server_setup.h"
#include "logic/room/team_room.h"
#include "logic/room/team_room_manager.h"
#include "rpc/dtmq/dtmq_client_subscriber.h"
#include "rpc/dtmq/dtmqproxysvrservice.atfw.gen.h"
#include "rpc/rpc_context.h"
#include "rpc/rpc_shared_message.h"
#include "utility/protobuf_mini_dumper.h"

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

// 向 repeated 共享数据字段追加一条 key-value(Any 仅设置 value 字节)；
// 返回 entry 便于调用方继续设置 permission/type_url
inline atfw::team::DTeamAnyDataWithKey* add_team_any_data_entry(
    google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* field, int64_t key, const std::string& value) {
  auto* entry = field->Add();
  entry->set_key(key);
  entry->mutable_value()->mutable_data()->set_value(value);
  return entry;
}

// 向 repeated 条件数据字段追加一条 key-value(DTeamAnyValueWithKey 的 Any 仅设置 value 字节)
inline atfw::team::DTeamAnyValueWithKey* add_team_any_value_entry(
    google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyValueWithKey>* field, int64_t key,
    const std::string& value) {
  auto* entry = field->Add();
  entry->set_key(key);
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
  }

  void advance(std::chrono::system_clock::duration by) {
    atfw::util::time::time_utility::set_global_now_offset(atfw::util::time::time_utility::get_global_now_offset() + by);
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
  bool foreach_team_action(Fn&& fn) const {
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

    // compact: 小于 compact_sequence 的日志从缓存移除(哈希链继续延伸)
    if (req.compact_sequence() > last_removed_sequence_) {
      last_removed_sequence_ = req.compact_sequence();
      size_t remove_count = 0;
      for (const auto& message : journal_) {
        if (message.sequence() <= last_removed_sequence_) {
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

    if (0 != team_room_manager::me()->init()) {
      CASE_MSG_INFO() << "team_room_manager init failed\n";
      return false;
    }

    // 时间轮是进程级单例且 init 幂等(last_tick_ 只前进不后退): 大时间偏移用例会把它推到未来，
    // 导致后续用例的房间定时器被钳制到 last_tick_+1 永不触发。每个环境启动时重建时间轮隔离。
    // 注意必须在时间基线前移之前重置: 重置后 last_tick_=当前真实now(即基线前移前)，
    // 过期/立即触发(==now)的定时器项才不会被钳制到"未来1秒"而错过本次驱动
    team_room_manager::me()->reset_timer_wheel_for_test();

    // 单调前移虚拟时间基线(见 mutable_process_time_floor 注释)
    auto& time_floor = mutable_process_time_floor();
    time_floor += std::chrono::seconds{120};
    atfw::util::time::time_utility::set_global_now_offset(time_floor);
    return true;
  }

  int stop() {
    bool has_unconsumed_fault = !personal_send_plan_.empty() || subscribe_fail_times_ > 0;
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
    return 0 != ret ? ret : (has_unconsumed_fault ? -1 : 0);
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
    // 频道 ID 由 team_api::make_team_room_channel_key 生成的标准单播格式: "channel:<type>:<zone_id>:D<team_id>"
    // (实例段带 D/N 前缀以区分数字/名字实例)
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
      if (!team_text.empty() && (team_text.front() == 'D' || team_text.front() == 'N')) {
        team_text = team_text.substr(1);
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

  // ---- driving helpers ----

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
  team_room::ptr_t setup_ready_room(const atfw::team::DTeamKey& team_key) {
    team_room::ptr_t room;
    int32_t ret = run("setup_ready_room", [&team_key, &room](rpc::context& ctx) -> rpc::result_code_type {
      room = team_room_manager::me()->mutable_room(ctx, team_key);
      if (!room) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
      }
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->await_ready(ctx)));
    });
    if (0 != ret) {
      room.reset();
    }
    return room;
  }

  team_room::ptr_t setup_ready_room(int64_t team_id) { return setup_ready_room(make_team_key(team_id)); }

  // 创建队伍的完整流程: 就绪 -> create_team。返回 create_team 结果码；room 传出。
  int32_t setup_created_team(int64_t team_id, const PROJECT_NAMESPACE_ID::DUserIDKey& owner_key,
                             const atfw::dtmq::DChannelIdKey& owner_channel, team_room::ptr_t* out_room = nullptr,
                             const atfw::team::DTeamConfigure* configure = nullptr) {
    return run("setup_created_team",
               [team_id, &owner_key, &owner_channel, out_room, configure](rpc::context& ctx) -> rpc::result_code_type {
                 auto room = team_room_manager::me()->mutable_room(ctx, make_team_key(team_id));
                 if (!room) {
                   RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
                 }
                 if (nullptr != out_room) {
                   *out_room = room;
                 }
                 int32_t ready_ret = RPC_AWAIT_CODE_RESULT(room->await_ready(ctx));
                 if (0 != ready_ret) {
                   RPC_RETURN_CODE(ready_ret);
                 }
                 atfw::team::SSTeamRoomCreateReq req;
                 protobuf_copy_message(*req.mutable_team_key(), make_team_key(team_id));
                 protobuf_copy_message(*req.mutable_sender_user_key(), owner_key);
                 protobuf_copy_message(*req.mutable_sender_user_channel(), owner_channel);
                 if (nullptr != configure) {
                   protobuf_copy_message(*req.mutable_configure(), *configure);
                 }
                 RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(room->create_team(ctx, req)));
               });
  }

  // 把 journal 中未下发的新日志推送给订阅者，并执行与 task_action_channel_event_sync 相同的
  // manager flush。事件回调在泵循环中异步完成(与真实 dispatcher 时序一致)，推送后先泵几轮再 flush。
  // force_snapshot=true 时改推全量快照(恢复/重置场景)。
  int32_t sync(const atfw::team::DTeamKey& team_key, bool force_snapshot = false) {
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
  void clear_rooms() {
    if (!team_room_manager::is_instance_destroyed()) {
      team_room_manager::me()->clear();
    }
  }

 private:
  template <class Predicate>
  bool wait_until(Predicate&& predicate, std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
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

          auto* fake = self->find_channel_by_key(typed_request.channel_key());
          if (nullptr == fake) {
            // 个人频道发送使用 no-wait stream，没有业务响应。这里编排的是远端处理结果:
            // commit_first=false 表示处理前丢弃；commit_first=true 表示先接收消息、随后处理失败。
            if (!self->personal_send_plan_.empty()) {
              auto plan = self->personal_send_plan_.front();
              self->personal_send_plan_.pop_front();
              if (!plan.first) {
                RPC_RETURN_CODE(plan.second);
              }
              personal_message_record record;
              record.channel.CopyFrom(typed_request.channel_key());
              if (typed_request.message_content().detail().has_event()) {
                (void)typed_request.message_content().detail().event().UnpackTo(&record.action);
              }
              self->personal_messages_.push_back(std::move(record));
              if (plan.second != 0) {
                RPC_RETURN_CODE(plan.second);
              }
              typed_response.set_client_result(0);
              RPC_RETURN_CODE(0);
            }
            personal_message_record record;
            record.channel.CopyFrom(typed_request.channel_key());
            if (typed_request.message_content().detail().has_event()) {
              (void)typed_request.message_content().detail().event().UnpackTo(&record.action);
            }
            self->personal_messages_.push_back(std::move(record));
            typed_response.set_client_result(0);
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
              typed_response.set_last_sequence(fake->last_sequence_);
              typed_response.set_last_hash_code(fake->last_hash_);
              RPC_RETURN_CODE(0);
            }
          }

          if (fault.present /* && fault.commit_first */) {
            // 已提交但响应丢失: 先落 journal 再以错误响应
            (void)fake->append_log([&typed_request](atfw::dtmq::DChannelMessageDetail& detail) {
              protobuf_copy_message(detail, typed_request.message_content().detail());
            });
            RPC_RETURN_CODE(fault.error_code);
          }

          auto* message = fake->append_log([&typed_request](atfw::dtmq::DChannelMessageDetail& detail) {
            protobuf_copy_message(detail, typed_request.message_content().detail());
          });
          typed_response.set_client_result(0);
          typed_response.set_message_sequence(message->sequence());
          typed_response.set_last_sequence(fake->last_sequence_);
          typed_response.set_last_hash_code(fake->last_hash_);
          RPC_RETURN_CODE(0);
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

          auto* fake = self->find_channel_by_key(typed_request.channel_key());
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
              typed_response.set_last_sequence(fake->last_sequence_);
              typed_response.set_last_hash_code(fake->last_hash_);
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
          typed_response.set_last_sequence(fake->last_sequence_);
          typed_response.set_last_hash_code(fake->last_hash_);
          RPC_RETURN_CODE(0);
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

          auto* fake = self->find_channel_by_key(typed_request.channel_key());
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
          RPC_RETURN_CODE(0);
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

 private:
  room_test_cfg_values cfg_;
  std::unique_ptr<atframework::testing::runtime> runtime_;
  std::unordered_map<atfw::team::DTeamKey, std::shared_ptr<fake_team_room_channel>,
                     rpc::team::team_api::team_key_hash_t, rpc::team::team_api::team_key_equal_t>
      channels_;
  std::vector<personal_message_record> personal_messages_;
  size_t send_message_handler_attempts_ = 0;
  std::deque<std::pair<bool, int32_t>> personal_send_plan_;
  uint32_t subscribe_fail_times_ = 0;
  int32_t subscribe_fail_code_ = PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE;

  atframework::testing::ss_rule_handle subscribe_rule_;
  atframework::testing::ss_rule_handle send_message_rule_;
  atframework::testing::ss_rule_handle update_rule_;
  atframework::testing::ss_rule_handle reset_lock_rule_;
  atframework::testing::ss_rule_handle destroy_rule_;
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
