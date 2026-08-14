// Copyright 2026 atframework

#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <dispatcher/task_type_traits.h>
#include <mem_pool/lru_map.h>
#include <memory/rc_ptr.h>
#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_common_types.h>
#include <time/jiffies_timer.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.dtmq.common.pb.h>
#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <data/user_key_hash_helper.h>

#include <cstdint>
#include <string>

namespace rpc {
class context;
}  // namespace rpc

class team_room_manager;

// 房间定时器事件类型。
// 注意: 以后新增的定时流程(如队长被踢出后的竞选流程、开始匹配、进入战斗房间等)
// 在此扩展事件类型，并在 team_room::get_next_timer_event 中给出各自的触发时间点。
enum class team_room_timer_event_type : int32_t {
  kNone = 0,
  kAcquireLock,        // 非主控节点在当前乐观锁过期后尝试接管(容灾切换)
  kMaintenance,        // 主控节点定期维护: 乐观锁续租+过期数据清理+日志压缩(底层一次 send_update)
  kKickOfflineMember,  // 剔除最久未心跳的成员(若踢出的是队长会自动触发换队长)
  kDestroyEmptyRoom,   // 空队伍保留到期后解散
  kDestroyChannel,     // 已解散队伍销毁频道
};

struct team_room_timer_event {
  team_room_timer_event_type type = team_room_timer_event_type::kNone;
  int64_t timepoint = 0;  // 触发时间点(秒)
};

// 组队房间对象。每个队伍对应一个 dtmq 频道(EN_TEAM_CHANNEL_TYPE_TEAM_ROOM)，
// 本节点通过 client_subscriber 订阅频道并竞争乐观锁成为队伍主控节点，
// 主控节点负责处理写请求、定期压缩频道日志并清理过期数据。
//
// 每个房间在 team_room_manager 的时间轮上有且只有一个定时器(除非房间被销毁)，
// 定时器总是指向房间的下一个定时 action(见 get_next_timer_event)。
class team_room : public atfw::util::memory::enable_shared_rc_from_this<team_room> {
 public:
  using ptr_t = atfw::util::memory::strong_rc_ptr<team_room>;
  using timer_watcher_t = atfw::util::time::jiffies_timer<>::timer_wptr_t;

  struct member_runtime_data {
    int64_t last_heartbeat_timepoint = 0;
    uint64_t user_router_server_id = 0;
  };
  // 最近访问成员 LRU: front 为最久未心跳的成员，用于计算下一个要踢出的成员
  using member_runtime_lru_map_t = atfw::util::mempool::lru_map<PROJECT_NAMESPACE_ID::DUserIDKey, member_runtime_data,
                                                                user_key_hash_t, user_key_equal_t>;

  team_room(int64_t team_id, std::string&& subscriber_key, std::string&& lock_holder);

  // 创建频道订阅者并设置回调
  int32_t create(rpc::context& ctx);

  int64_t get_team_id() const noexcept;
  const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept;
  // 频道订阅已就绪(首轮数据已同步)
  bool is_subscriber_ready() const noexcept;
  // 本节点是否持有乐观锁(为队伍主控节点)
  bool is_master() const noexcept;
  bool is_destroyed() const noexcept;
  // 房间对象是否可以回收
  bool ready_to_destroy() const noexcept;

  // 获取下一个定时器事件(剔除长期无心跳成员、日志压缩、续租等)
  team_room_timer_event get_next_timer_event(int64_t now);
  // 定时器回调入口，由 team_room_manager 的时间轮驱动
  void on_timer(rpc::context& ctx);
  // 重新计算下一个定时器事件并重设本房间的唯一定时器
  void schedule_next_timer();

  // 等待订阅首轮数据就绪(协程内调用)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_ready(rpc::context& ctx);

  // 提交组队操作(协程内调用)，来自外部服务的写请求统一经由此处写入频道日志
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_action(rpc::context& ctx,
                                                                 const atframework::team::DTeamAction& action);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_member_action(
      rpc::context& ctx, const atframework::team::DTeamMemberAction& action);
  // 成员心跳，更新成员确认位点和在线簿记(协程内调用)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type heartbeat(rpc::context& ctx,
                                                               const atframework::team::SSTeamRoomHeartbeatReq& req);

  // 频道销毁通知后由 manager 回收前调用
  void on_remove();

  // 订阅者事件回调(经 local_private_data 回传 this，由共享回调组转发)
  void on_ready(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber);
  void on_receive_event(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                        const ::atfw::dtmq::DChannelMessage& message);
  void on_update_optimistic_lock(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                 const ::atfw::dtmq::DChannelOptimisticLock& from,
                                 const ::atfw::dtmq::DChannelOptimisticLock& to);
  void on_destroyed(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber);

 private:
  friend class team_room_manager;

  // 从订阅数据恢复队伍快照(custom_data + 增量日志 + private_data)
  void restore_snapshot(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber);
  // 应用频道事件到本地状态，所有应用操作必须幂等
  void apply_action(rpc::context& ctx, const atframework::team::DTeamAction& action, int64_t sequence,
                    uint64_t hash_code);
  void apply_member_action(rpc::context& ctx, const atframework::team::DTeamMemberAction& action, int64_t sequence,
                           uint64_t hash_code);
  void apply_event_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& message);
  void update_acknowledge(int64_t sequence, uint64_t hash_code);

  atframework::team::DTeamMember* mutable_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);
  const atframework::team::DTeamMember* find_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const;
  // 队长退出后在剩余成员中确定性地选出新队长(加入时间最早者)
  void elect_captain_after_remove(const PROJECT_NAMESPACE_ID::DUserIDKey& removed_user_key);
  // 成员离线过期时间点(无心跳簿记时回退到入队时间/快照恢复时间)
  int64_t get_member_offline_deadline(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);
  // 成员清单变为空/非空时刷新空房间计时
  void refresh_empty_tracking(int64_t now);

  // 乐观锁: 本节点 CAS 接管频道(新节点订阅后携带老锁切换，老节点再写入时锁失败)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type acquire_lock(rpc::context& ctx);
  void handle_lock_conflict(rpc::context& ctx, const ::atfw::dtmq::DChannelOptimisticLock& real_lock);
  void step_down();
  ::atfw::dtmq::DChannelOptimisticLock make_self_lock(int64_t now) const;
  atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker> make_write_lock_checker() const;
  // 携带乐观锁检查写入频道事件，锁冲突时自动退位
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_event_with_lock(rpc::context& ctx,
                                                                          ::google::protobuf::Any&& event_data);

  // 定时器事件执行入口
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type execute_timer_event(rpc::context& ctx,
                                                                         team_room_timer_event_type event_type);
  // 主控节点定期维护: 过期数据清理 + 一次 send_update(乐观锁续租 + 按需压缩日志)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type do_maintenance(rpc::context& ctx);
  // 清理过期邀请和过期加入请求
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type cleanup_expired_admissions(rpc::context& ctx, int64_t now);
  // 踢出所有离线过期成员(LRU 从最久未心跳的成员开始)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type kick_due_offline_members(rpc::context& ctx, int64_t now);
  // 解散空队伍
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type destroy_empty_room(rpc::context& ctx);
  void dump_private_data(atframework::team::DTeamRoomPrivateData& output) const;

  // 乐观锁租约时长，不低于 dtmq 频道配置的订阅者心跳过期淘汰时间(subscriber_timeout)
  int64_t get_lock_lease_sec() const;
  // 乐观锁续租间隔，按租约时长折半
  int64_t get_lock_renew_interval_sec() const;
  // 触发日志压缩的日志数量阈值，取自 dtmq 频道配置 gc_log_count
  int64_t get_compact_log_count() const;

  int64_t team_id_;
  atfw::dtmq::DChannelIdKey channel_key_;
  std::string subscriber_key_;
  // 本节点乐观锁持有者标识，与服务节点名和节点ID相关
  std::string lock_holder_;
  rpc::dtmq::client_subscriber::ptr_t subscriber_;

  // 权威队伍状态，随 custom_data 同步给所有订阅者(成员清单、加入请求和加入邀请列表)
  atframework::team::DTeamStorage storage_;
  // 成员心跳等在线簿记(LRU 维护最近访问成员)，随 private_data 仅在主控节点间同步，不下发给成员
  member_runtime_lru_map_t member_heartbeats_;

  // 本节点最近一次设置或看到的乐观锁
  ::atfw::dtmq::DChannelOptimisticLock current_lock_;
  bool lock_acquired_ = false;
  bool destroyed_ = false;
  bool channel_destroyed_ = false;
  bool channel_destroy_sent_ = false;
  bool snapshot_restored_ = false;
  int64_t restore_timepoint_ = 0;

  int64_t empty_since_timepoint_ = 0;
  int64_t next_renew_lock_timepoint_ = 0;
  int64_t next_compact_timepoint_ = 0;
  int64_t last_compact_sequence_ = 0;
  int64_t last_compact_timepoint_ = 0;
  task_type_trait::task_type maintenance_task_;
  // 本房间在 manager 时间轮上的唯一定时器
  timer_watcher_t timer_watcher_;
};
