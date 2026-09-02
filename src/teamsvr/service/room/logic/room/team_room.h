// Copyright 2026 atframework

#pragma once

#include <config/compile_optimize.h>

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>

#include <memory/object_atfw_memory_lru_map.h>

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
#include <rpc/team/team_key_hash_helper.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
  // 触发时间点
  std::chrono::system_clock::time_point timeout = std::chrono::system_clock::from_time_t(0);
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
    std::chrono::system_clock::time_point last_heartbeat_timepoint = std::chrono::system_clock::from_time_t(0);
    uint64_t user_router_server_id = 0;

    atfw::team::DTeamMember member_data;
    // 成员共享数据的 key-value 索引(内存中以 unordered_map 维护；member_data.shared_member_data 字段
    // 在内存中恒为空，仅在 dump 快照/构建频道事件时按 key 回填)
    std::unordered_map<int64_t, atfw::team::DTeamAnyData> shared_member_data;

    atfw::team::EnTeamExitReason exit_reason = atfw::team::EN_TEAM_EXIT_REASON_DEFAULT;
  };

  struct member_retry_data {
    std::chrono::system_clock::time_point next_retry_timepoint = std::chrono::system_clock::from_time_t(0);
    uint32_t retry_times = 0;
  };

  using member_ptr_t = atfw::util::memory::strong_rc_ptr<member_runtime_data>;
  // 最近访问成员 LRU: front 为最久未心跳的成员，用于计算下一个要踢出的成员
  using member_runtime_lru_map_t =
      atfw::component::memory::util::lru_map_st<PROJECT_NAMESPACE_ID::DUserIDKey, member_runtime_data, user_key_hash_t,
                                                user_key_equal_t>;

  // 重试队列LRU map
  using member_retry_lru_map_t =
      atfw::component::memory::util::lru_map_st<PROJECT_NAMESPACE_ID::DUserIDKey, member_retry_data, user_key_hash_t,
                                                user_key_equal_t>;

  using invitation_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamInvitation>;
  using invitation_map_t =
      std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, invitation_ptr_t, user_key_hash_t, user_key_equal_t>;

  using join_request_ptr_t = atfw::util::memory::strong_rc_ptr<atfw::team::DTeamJoinRequest>;
  using join_request_map_t =
      std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, join_request_ptr_t, user_key_hash_t, user_key_equal_t>;

  using pending_team_member_message_t = std::pair<atfw::dtmq::DChannelIdKey, atfw::team::DTeamMemberAction>;
  using pending_team_member_channel_t =
      std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, std::list<pending_team_member_message_t>, user_key_hash_t,
                         user_key_equal_t>;

 private:
  struct ctor_guard {};

 public:
  team_room(ctor_guard&, const atfw::team::DTeamKey& team_key, const atfw::dtmq::DChannelIdKey& channel_key,
            std::string&& subscriber_key, std::string&& lock_holder,
            atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&& subscriber);
  team_room(const team_room&) = delete;
  team_room& operator=(const team_room&) = delete;
  team_room(team_room&&) = delete;
  team_room& operator=(team_room&&) = delete;

  // 创建频道订阅者并设置回调
  static team_room::ptr_t create(rpc::context& ctx, const atfw::team::DTeamKey& team_key);

  const atfw::team::DTeamKey& get_team_key() const noexcept;

  uint32_t get_team_type() const noexcept;

  const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept;
  // 频道订阅已就绪(首轮数据已同步)
  bool is_subscriber_ready() const noexcept;
  // 本节点是否持有乐观锁(为队伍主控节点)
  bool is_lock_holder() const noexcept;

  // 获取下一个定时器事件(剔除长期无心跳成员、日志压缩、续租等)
  team_room_timer_event get_next_timer_event(std::chrono::system_clock::time_point now);
  // 定时器回调入口，由 team_room_manager 的时间轮驱动
  void on_timer(rpc::context& ctx);
  // 重新计算下一个定时器事件并重设本房间的唯一定时器
  void schedule_next_timer();

  // 等待订阅首轮数据就绪(协程内调用)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_ready(rpc::context& ctx);

  // 创建队伍(协程内调用): 初始化配置与共享队伍数据，创建者作为队长(owner)入队并记录其个人通知频道
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type create_team(rpc::context& ctx,
                                                                 const atfw::team::SSTeamRoomCreateReq& req);
  // 发起邀请(协程内调用): 写入 add_invitation 频道事件，事件应用后推送被邀请人个人通知频道
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_invitation(rpc::context& ctx,
                                                                    const atfw::team::SSTeamRoomAddInvitationReq& req);
  // 接受邀请(协程内调用): 写入 approve_invitation 频道事件，事件应用后被邀请人入队并收到通知
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type approve_invitation(
      rpc::context& ctx, const atfw::team::SSTeamRoomApproveInvitationReq& req);
  // 拒绝邀请(协程内调用): 写入 reject_invitation 频道事件，事件应用后移除邀请并通知邀请人
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_invitation(
      rpc::context& ctx, const atfw::team::SSTeamRoomRejectInvitationReq& req);
  // 发起加入请求(协程内调用): 写入 add_join_request 频道事件，事件应用后通知队长
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type add_join_request(
      rpc::context& ctx, const atfw::team::SSTeamRoomAddJoinRequestReq& req);
  // 批准加入请求(协程内调用): 写入 approve_join_request 频道事件，事件应用后申请人入队并收到通知
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type approve_join_request(
      rpc::context& ctx, const atfw::team::SSTeamRoomApproveJoinRequestReq& req);
  // 拒绝加入请求(协程内调用): 写入 reject_join_request 频道事件，事件应用后移除申请并通知申请人
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type reject_join_request(
      rpc::context& ctx, const atfw::team::SSTeamRoomRejectJoinRequestReq& req);

  // 校验外部请求的操作权限(成员身份与角色门槛，见 DTeamConfigure；member_update/team_update 还需通过
  // 其携带的 DTeamConditionChecker 数据条件检查，未通过返回 EN_ERR_TEAM_CONDITION_NOT_MATCH)，
  // 通过返回 0，否则返回错误码且不提交频道事件。
  // 条件检查中 Any 解包的临时对象分配在 ctx 绑定的任务 arena 上，减少内存碎片
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type check_action_permission(
      rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& operator_key, const atfw::team::DTeamAction& action);

  // 提交组队操作(协程内调用)，来自外部服务的写请求统一经由此处写入频道日志
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_action(rpc::context& ctx,
                                                                 const atfw::team::DTeamAction& action,
                                                                 bool no_wait = false);
  // 批量提交组队操作(协程内调用): 同一流程的多条事件合并为一次频道写入，日志按列表顺序追加
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_actions(
      rpc::context& ctx, gsl::span<const atfw::team::DTeamAction* const> actions, bool no_wait = false);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_member_action(rpc::context& ctx,
                                                                        const atfw::dtmq::DChannelIdKey& channel_key,
                                                                        const atfw::team::DTeamMemberAction& action);
  // 批量向成员个人通知频道发送消息(协程内调用): 同一频道的多条通知合并为一次发送
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_member_actions(
      rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key,
      gsl::span<const atfw::team::DTeamMemberAction* const> actions);
  // 成员心跳，更新成员已确认的日志序号和在线状态记录(协程内调用)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type heartbeat(rpc::context& ctx,
                                                               const atfw::team::SSTeamRoomHeartbeatReq& req);

  // 频道销毁通知后由 manager 回收前调用
  void on_remove();

  // 订阅者事件回调(经 local_private_data 回传 this，由共享回调组转发)
  void on_receive_snapshot_finished(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                    int32_t result_code);
  // 频道消息回调(经 local_private_data 回传 this，由共享回调组转发)。
  // 注册在 on_receive_raw_message 上，所有 command_case(event/text/create/destroy/reset_lock)都会到达，
  // 以保证 ack 与最老未压缩日志时间点覆盖全部日志(锁续租也会产生 kResetLock 日志)
  void on_receive_raw_message(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                              const ::atfw::dtmq::DChannelMessage& message);
  void on_update_optimistic_lock(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                 const ::atfw::dtmq::DChannelOptimisticLock& from,
                                 const ::atfw::dtmq::DChannelOptimisticLock& to);
  void on_destroyed(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber);

  static std::chrono::system_clock::duration get_room_destroy_delay() noexcept;

  void change_captain(const PROJECT_NAMESPACE_ID::DUserIDKey& new_captain_key,
                      atfw::team::EnTeamPermissionRole set_role);

  rpc::result_code_type flush_pending_channel_message(rpc::context& ctx);

  void dump_team_key(atfw::team::DTeamKey& output) const;

  member_ptr_t find_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool update_visit);

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  // 测试钩子: 成员 LRU 从 front(最久未访问)到 back 的 user key 顺序(RCV-01 恢复顺序断言)
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> debug_member_lru_keys() const;
  // 测试钩子: 本地已生效的压缩边界与快照覆盖序号(CMP 用例)
  int64_t debug_last_compact_sequence() const noexcept;
  int64_t debug_saved_action_sequence() const noexcept;
  // 测试钩子: 已应用事件日志的 ack 游标(只前进不回退; EVT-01/11 消费进度断言)
  int64_t debug_acknowledge_action_sequence() const noexcept;
  // 测试钩子: 当前定时器到期时间(0 表示无定时器，LIFE-08 定时器单调性断言)
  std::chrono::system_clock::time_point debug_timer_timeout() const noexcept;
  // 测试钩子: 移除重试队列与待发个人通知数量(LIFE-04/11、LCK-05 断言)
  size_t debug_retry_remove_count() const;
  size_t debug_pending_notification_count() const;
  bool debug_maintenance_task_running() const noexcept;
#endif

 private:
  friend class team_room_manager;

  // 从订阅数据恢复队伍快照(custom_data + 增量日志 + private_data)
  ATFW_EXPLICIT_NODISCARD_ATTR bool restore_snapshot(rpc::context& ctx);
  // 应用频道事件到本地状态，所有应用操作必须幂等
  void apply_action(rpc::context& ctx, atfw::team::DTeamAction& action, int64_t sequence, uint64_t hash_code,
                    std::chrono::system_clock::time_point event_timepoint);
  ATFW_EXPLICIT_NODISCARD_ATTR bool apply_event_message(rpc::context& ctx,
                                                        const ::atfw::dtmq::DChannelMessage& message);
  void update_acknowledge(int64_t sequence, uint64_t hash_code);
  // 应用成员入队(幂等): 已存在的成员保留其 key 与更早的入队时间，首位成员成为队长。
  // 入队/心跳时间缺失时以 event_timepoint(频道事件创建时间)兜底，保证各节点状态一致
  void apply_add_member(rpc::context& ctx, atfw::team::DTeamMember&& member_data,
                        std::chrono::system_clock::time_point event_timepoint);

  // apply_action 的各事件分支处理(拆分为独立函数以降低单个函数复杂度)
  void apply_member_update(const atfw::team::DTeamMemberUpdateData& update_data);
  void apply_team_update(const atfw::team::DTeamUpdateData& update_data);
  void apply_member_set_role(const atfw::team::DTeamMemberSetRole& set_role);
  void apply_add_invitation(rpc::context& ctx, const atfw::team::DTeamInvitation& invitation);
  void apply_approve_invitation(const atfw::team::DTeamInvitation& invitation);
  void apply_reject_invitation(const atfw::team::DTeamInvitation& invitation);
  void apply_add_join_request(rpc::context& ctx, const atfw::team::DTeamJoinRequest& join_request);
  void apply_approve_join_request(const atfw::team::DTeamJoinRequest& join_request);
  void apply_reject_join_request(const atfw::team::DTeamJoinRequest& join_request);

  member_ptr_t mutable_member(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);
  bool remove_member(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                     atfw::team::EnTeamExitReason reason, bool with_notify);
  void foreach_member(
      rpc::context& ctx,
      atfw::util::nostd::function_ref<bool(rpc::context&, atfw::util::nostd::nonnull<const member_ptr_t>&)> fn);

  bool resolve_max_member_count(rpc::context& ctx);
  bool resolve_max_join_request_count(rpc::context& ctx);
  bool resolve_max_invitation_count(rpc::context& ctx);

  void append_team_member_channel_notification(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                               atfw::dtmq::DChannelIdKey&& channel_id,
                                               atfw::team::DTeamMemberAction&& action);

  // 队长退出后在剩余成员中确定性地选出新队长，并发送 election_captain 频道事件
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type elect_captain_after_remove(rpc::context& ctx);
  // 成员离线过期时间点(无心跳记录时回退到入队时间/快照恢复时间)
  std::chrono::system_clock::time_point get_member_offline_deadline(const member_runtime_data& member_data);
  // 成员清单变为空/非空时刷新空房间计时
  void refresh_empty_tracking(std::chrono::system_clock::time_point now);

  // 乐观锁: 本节点 CAS 接管频道(新节点订阅后携带老锁切换，老节点再写入时锁失败)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type acquire_lock(rpc::context& ctx);
  void handle_lock_conflict(rpc::context& ctx, const ::atfw::dtmq::DChannelOptimisticLock& real_lock);
  void step_down();

  ::atfw::dtmq::DChannelOptimisticLock make_self_lock(std::chrono::system_clock::time_point now) const;
  atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker> make_write_lock_checker() const;
  // 构建抢占写锁的 CAS 检查器(空锁/过期锁/本节点锁才接受重置); 视图中有未过期他人锁时返回
  // EN_ERR_DTMQ_CHANNEL_LOCK_FAILED 并回带真实锁
  ATFW_EXPLICIT_NODISCARD_ATTR int32_t
  make_acquire_lock_checker(std::chrono::system_clock::time_point now, ::atfw::dtmq::DChannelOptimisticLock& self_lock,
                            atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker>& checker);
  // 携带乐观锁检查写入频道事件，锁冲突时自动退位
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_event_with_lock(rpc::context& ctx,
                                                                          ::google::protobuf::Any&& event_data,
                                                                          bool no_wait);
  // 携带乐观锁检查批量写入频道事件(一次请求追加多条)，锁冲突时自动退位
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type send_events_with_lock(
      rpc::context& ctx, google::protobuf::RepeatedPtrField<::google::protobuf::Any>&& events, bool no_wait);
  // 打包并发送 DTeamAction 频道事件(协程内调用)，不做移除成员的去重与重试队列处理
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type do_send_action(rpc::context& ctx,
                                                                    const atfw::team::DTeamAction& action,
                                                                    bool no_wait);
  // 打包并批量发送 DTeamAction 频道事件(协程内调用)，不做移除成员的去重与重试队列处理
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type do_send_actions(
      rpc::context& ctx, gsl::span<const atfw::team::DTeamAction* const> actions, bool no_wait);

  // 定时器事件执行入口
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type execute_timer_event(rpc::context& ctx,
                                                                         team_room_timer_event_type event_type);
  // 主控节点定期维护: 过期数据清理 + 一次 send_update(乐观锁续租 + 按需压缩日志)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type do_maintenance(rpc::context& ctx);
  // 清理过期邀请和过期加入请求
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type cleanup_expired_admissions(
      rpc::context& ctx, std::chrono::system_clock::time_point now);
  // 踢出所有离线过期成员(LRU 从最久未心跳的成员开始)
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type kick_due_offline_members(
      rpc::context& ctx, std::chrono::system_clock::time_point now);
  // 解散空队伍
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type destroy_empty_room(rpc::context& ctx);

  void dump_private_data(rpc::context& ctx, atfw::team::DTeamRoomPrivateData& output, int64_t compact_sequence,
                         std::chrono::system_clock::time_point compact_timepoint);
  // dump 公共快照；GAP-08: 已过期的邀请/加入请求不写入快照(now 为判定基准)
  void dump_public_data(rpc::context& ctx, atfw::team::DTeamStorage& output, std::chrono::system_clock::time_point now);

  // 乐观锁租约时长，不低于 dtmq 频道配置的订阅者心跳过期淘汰时间(subscriber_timeout)
  std::chrono::system_clock::duration get_lock_lease() const;
  // 乐观锁续租间隔，按租约时长折半
  std::chrono::system_clock::duration get_lock_renew_interval() const;
  // dtmq 频道配置的保留日志数 gc_log_count，按数量维度压缩的基准
  int64_t get_gc_log_count() const;
  // 按时间维度压缩时保留的日志时长，缺省取开始压缩时长的一半
  std::chrono::system_clock::duration get_compact_log_keep_time() const;
  // 从本地缓存刷新最早的未压缩日志时间点(用于按时间维度压缩的调度与触发)。
  // sequence 只保证递增不保证连续，用 query_cached_message 二分查找第一条不早于压缩点的日志
  void refresh_oldest_log_timepoint(rpc::context& ctx);
  // 计算本次维护的日志压缩点(0 表示不可压缩)。每次 send_update 都会尝试压缩以减少快照数据量:
  // 保留最近若干条日志(keep_percent/keep_count)，且保留 compact_log_keep_time 窗口内的日志
  int64_t pick_compact_sequence(rpc::context& ctx, std::chrono::system_clock::time_point now);

  // 操作角色门槛(DTeamConfigure 可配置，GUEST 表示使用默认值)
  atfw::team::EnTeamPermissionRole get_manage_member_role() const;         // 默认 ADMIN
  atfw::team::EnTeamPermissionRole get_approve_join_request_role() const;  // 默认 NORMAL
  atfw::team::EnTeamPermissionRole get_invite_role() const;                // 默认 NORMAL
  atfw::team::EnTeamPermissionRole get_update_team_data_role() const;      // 默认 NORMAL
  atfw::team::EnTeamPermissionRole get_reject_invitation_role() const;     // 默认 ADMIN
  atfw::team::EnTeamPermissionRole get_set_member_role_role() const;       // 默认 ADMIN

  uint32_t get_max_member_count() const;        // 默认 3
  uint32_t get_max_join_request_count() const;  // 默认 30
  uint32_t get_max_invitation_count() const;    // 默认 30
  // 是否允许非成员发起加入请求(false 即私人小队，仅通过邀请加入)，默认允许
  bool is_join_request_allowed() const;

  // 更新条件检查(DTeamMemberUpdateData/DTeamUpdateData.condition): 列表为空表示无附加条件(直接通过)；
  // 各 checker 之间是或关系(任一通过即通过)，checker 内部各项数据是与关系
  bool check_update_conditions(rpc::context& ctx,
                               const google::protobuf::RepeatedPtrField<atfw::team::DTeamConditionChecker>& conditions);
  // 单个 checker(与关系): 共享队伍数据等值 + 成员数量范围 + 全部成员条件组
  bool check_condition_checker(rpc::context& ctx, const atfw::team::DTeamConditionChecker& checker);
  // 成员条件组: 按 scope(指定成员/全部/任意/数量/百分比门槛)统计满足 member_condition 的成员并判定
  bool check_member_condition_group(rpc::context& ctx,
                                    const atfw::team::DTeamConditionChecker::DMemberConditionGroup& group);

 private:
  atfw::team::DTeamKey team_key_;
  atfw::dtmq::DChannelIdKey channel_key_;
  std::string subscriber_key_;
  // 本节点乐观锁持有者标识，与服务节点名和节点ID相关
  std::string lock_holder_;
  atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t> subscriber_;

  // 权威队伍状态，随 custom_data 同步给所有订阅者(成员清单、加入请求和加入邀请列表)
  // 注意: storage_.shared_team_data 字段在内存中恒为空，共享队伍数据由 shared_team_data_ 维护，
  // dump 快照时按 key 回填
  atfw::team::DTeamStorage storage_;
  // 队伍共享数据的 key-value 索引(内存中以 unordered_map 维护)
  std::unordered_map<int64_t, atfw::team::DTeamAnyData> shared_team_data_;
  std::unordered_map<int64_t, atfw::team::DTeamAnyData> private_team_data_;
  // 成员心跳等在线状态记录(LRU 维护最近访问成员)
  member_runtime_lru_map_t member_;
  // 移除成员的重试队列(如果失败则重试)
  member_retry_lru_map_t member_retry_remove_;
  struct iterating_member_protect_t;
  iterating_member_protect_t* iterating_member_protect_ = nullptr;

  invitation_map_t pending_invitation_by_invitee_;
  join_request_map_t pending_join_request_by_requester_;

  pending_team_member_channel_t pending_member_channel_actions_;

  // 本节点最近一次设置或看到的乐观锁
  ::atfw::dtmq::DChannelOptimisticLock current_lock_;
  bool lock_acquired_ = false;
  bool destroyed_ = false;
  bool channel_destroy_sent_ = false;
  bool team_created_ = false;
  bool snapshot_restored_ = false;
  std::chrono::system_clock::time_point restore_timepoint_;

  std::chrono::system_clock::time_point empty_since_timepoint_;
  std::chrono::system_clock::time_point next_renew_lock_timepoint_;
  // GAP-07: 心跳等未产生频道日志的运行时数据变化标记。长期无新日志时新心跳不必即时持久化，
  // 但维护发现该标记后随续租 update 保存 custom/private 快照，保证负载迁移转出前数据已持久化
  bool runtime_data_dirty_ = false;
  // 最早的未压缩日志时间点(随日志压缩推进，用于按时间维度压缩的调度与触发)
  std::chrono::system_clock::time_point oldest_log_timepoint_;
  // 上一次维护开始时的未压缩日志数: 压缩加速触发要求自上次维护以来有显著新增日志(>= 保留下限)。
  // 均衡态下维护自身追加的续租/快照日志抵消裁剪量，日志数不增长，加速触发不会反复置为
  // "已过期"而饿死 kick/destroy 等更晚到期的定时事件; 突发写入带来的真实增长仍可及时触发压缩
  int64_t last_maintenance_uncompacted_count_ = 0;
  int64_t last_compact_sequence_ = 0;
  std::chrono::system_clock::time_point last_compact_timepoint_;
  task_type_trait::task_type maintenance_task_;
  // 本房间在 manager 时间轮上的唯一定时器
  timer_watcher_t timer_watcher_;
  std::chrono::system_clock::time_point timer_timeout_ = std::chrono::system_clock::from_time_t(0);
};
