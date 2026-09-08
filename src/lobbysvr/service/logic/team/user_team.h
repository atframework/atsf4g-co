// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>
#include <memory/rc_ptr.h>
#include <nostd/nullability.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/com.struct.team.pb.h>
#include <protocol/pbdesc/com.struct.team.shared.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <rpc/dtmq/dtmq_client_subscriber.h>

#include <data/user_key_hash_helper.h>

#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>

class user;
class user_team_manager;

class user_team : public atfw::util::memory::enable_shared_rc_from_this<user_team> {
 public:
  using ptr_t = atfw::util::memory::strong_rc_ptr<user_team>;

 private:
  struct ctor_guard_t;

  // 布尔状态标志合并为 bitset 存储, 减少每个队伍对象的内存占用; 语义注释见各枚举值
  // NOLINTNEXTLINE(readability-enum-initial-value)
  enum class team_flag : uint32_t {
    // 本人已是队伍成员
    kMember = 0,
    // 匹配中(由队伍共享数据模块处理器驱动的本地派生状态)
    kMatching,
    // 有待下发的快照脏数据(快照覆盖重放期间的全部增量)
    kPendingDirtySnapshot,
    // remove 已下发(或确认无需下发); 重新激活(add_team 经 make_current_actived)时复位
    kDirtyRemoveSent,
    // 客户端已收到过该队伍的数据(快照/增量/get-info 导出), 未收到过则无需下发 remove
    kClientAnnounced,
    // 对象已登记在 manager 的 team_index_ 中(由 user_team_manager 经 manager_accessor 维护)
    kIndexActive,
    kMax,
  };

  inline bool check_flag(team_flag flag) const noexcept { return flags_.test(static_cast<size_t>(flag)); }
  inline void set_flag(team_flag flag, bool value) noexcept { flags_.set(static_cast<size_t>(flag), value); }

 public:
  user_team(ctor_guard_t&, rpc::context& ctx, user_team_manager& owner,
            atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&& channel_subscriber, uint32_t team_type,
            const atfw::team::DTeamKey& team_key);

  ~user_team();

  static ptr_t create(rpc::context& ctx, user_team_manager& owner, uint32_t team_type,
                      const atfw::team::DTeamKey& team_key, const atfw::dtmq::DChannelIdKey& channel_key);

  void init_cached_data(const PROJECT_NAMESPACE_ID::DUserIDKey& captain_user_key,
                        atfw::team::EnTeamPermissionRole permission_role);

  void dump(atfw::team::DTeamMemberJoinData& join_data) const;

  void dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::DUserTeamSnapshot& output) const;

  bool can_be_removed(rpc::context& ctx) const noexcept;

  bool wait_to_be_member_but_timeout(rpc::context& ctx) const noexcept;

  bool is_exiting() const noexcept;

  bool is_destroyed() const noexcept;

  // 对客户端来说等同于已移除: 已不是成员、正在退出、频道已销毁、已让出当前队伍或对象已被新的一代替换。
  // user_team_manager 的脏数据下发与 user_team 的增量脏数据准入共用这一判定，不要在别处重复展开
  bool is_removed_for_client() const noexcept;
  // 对象是否仍是 manager 索引中的当前代际。对象被收编/替换后, 迟到的频道事件、快照回调和销毁回调
  // 仍可能送达(订阅端私有数据存活期长于索引记录), 此时必须忽略, 防止污染新代际或重复下发。
  // 实现为 manager 在 team_index_ 增删时维护的标志位, 频道消息热路径不产生额外查找
  bool is_active_generation() const noexcept;
  // 本地派生状态: 由队伍共享数据模块的 do_update/do_delete 处理器驱动(当前为 battle.matching)
  inline bool is_matching() const noexcept { return check_flag(team_flag::kMatching); }

  inline bool is_member() const noexcept { return check_flag(team_flag::kMember); }

  inline uint32_t get_team_type() const noexcept { return team_type_; }

  inline const atfw::team::DTeamKey& get_team_key() const noexcept { return team_key_; }

  const atfw::dtmq::DChannelIdKey& get_channel_key() const noexcept;

  inline const PROJECT_NAMESPACE_ID::DUserIDKey& get_cached_captain_user_key() const noexcept {
    return cached_captain_user_key_;
  }

  inline atfw::team::EnTeamPermissionRole get_cached_permission_role() const noexcept {
    return cached_permission_role_;
  }
  // 最近一次退出/被移除的原因(由个人通知或本地退出流程记录,随退出重试复用)
  inline atfw::team::EnTeamExitReason get_last_exit_reason() const noexcept { return last_exit_team_reason_; }

  inline const atfw::team::DTeamConfigure& get_configure() const noexcept { return cached_configure_; }

  bool check_permission(atfw::team::EnTeamPermissionRole checked) const noexcept;

  void make_current_actived(rpc::context& ctx);

  // 清理本地缓存中已过期的加入请求和邀请(teamsvr-room 清理过期准入数据时不下发事件，各端按自身的
  // expired_timepoint 自行清理)。返回清理的条目数
  size_t cleanup_expired_admissions(rpc::context& ctx);

  // 成员心跳(内部按 heartbeat_interval 节流): 上报本端已确认的日志序号与在线状态，防止被 teamsvr-room
  // 按离线过期踢出
  void maybe_send_heartbeat(rpc::context& ctx);

  void send_exit_team_request(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason);

  // 以下操作转发到 teamsvr-room(按队伍一致性哈希路由)，业务结果经 client_result 透传返回
  rpc::result_code_type accept_join_request(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);

  rpc::result_code_type reject_join_request(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);

  rpc::result_code_type remove_member(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);

  rpc::result_code_type transfer_captain(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);

  rpc::result_code_type update_member_role(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                           atfw::team::EnTeamPermissionRole role);

  rpc::result_code_type update_team_shared_data(
      rpc::context& ctx, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DTeamSharedDataModule>& data);

  rpc::result_code_type update_member_shared_data(
      rpc::context& ctx, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule>& data);

  void set_exit_team(rpc::context& ctx, atfw::team::EnTeamExitReason exit_reason);

  void retry_send_exit_team_request(rpc::context& ctx);

  void try_load_snapshot(rpc::context& ctx);

  void async_flush_all_member_shared_data(rpc::context& ctx);

  void dump_dirty_data(rpc::context& ctx, PROJECT_NAMESPACE_ID::DUserTeamDirty& output);
  void clear_dirty_data(rpc::context& ctx);

  bool insert_dirty_snapshot_handle();
  bool insert_dirty_action_handle();

  // 面向 user_team_manager 的内部状态访问器。嵌套类天然可访问 user_team 私有成员,
  // 同时只把这几个入口开放给 user_team_manager, 替代整类 friend 的敞开暴露
  class manager_accessor {
   public:
    manager_accessor() = delete;

   private:
    friend class user_team_manager;

    // remove 已下发后, 退出重试、迟到事件与对象清理不再重复下发
    static inline bool is_dirty_remove_sent(const user_team& team) noexcept {
      return team.check_flag(team_flag::kDirtyRemoveSent);
    }
    static inline void set_dirty_remove_sent(user_team& team, bool value) noexcept {
      team.set_flag(team_flag::kDirtyRemoveSent, value);
    }
    // 客户端是否已收到过这个队伍的数据(快照/增量/get-info 导出)。未收到过则无需下发 remove
    static inline bool is_client_announced(const user_team& team) noexcept {
      return team.check_flag(team_flag::kClientAnnounced);
    }
    static inline void set_client_announced(user_team& team, bool value) noexcept {
      team.set_flag(team_flag::kClientAnnounced, value);
    }
    static void repair_from_room_error(user_team& team, rpc::context& ctx, int32_t result_code) {
      team.repair_from_room_error(ctx, result_code);
    }
    // 对象是否登记在 manager 索引中(由 manager 在 team_index_ 增删时维护, 供热路径零查找判断代际)
    static inline void set_index_active(user_team& team, bool value) noexcept {
      team.set_flag(team_flag::kIndexActive, value);
    }
  };

 private:
  // 打包队伍事件并按 team_key 一致性哈希路由发送到 teamsvr-room，返回透传的业务结果
  rpc::result_code_type send_action(rpc::context& ctx, atfw::team::DTeamAction&& action);

  void repair_from_room_error(rpc::context& ctx, int32_t result_code);

  bool load_dtmq_custom_data(rpc::context& ctx, const ::google::protobuf::Any& custom_data);

  bool load_team_action(rpc::context& ctx, const ::atfw::team::DTeamAction& action, bool refresh_member_data);

  // 追加一条增量脏数据(解包成员共享数据以便客户端直接使用)。快照脏数据待下发时无需追加，
  // 快照会覆盖重放期间的全部变更
  void append_pending_dirty_action(rpc::context& ctx, const ::atfw::team::DTeamAction& action);

  // 全量替换/合并成员缓存(重复的 add_member 保留更早的入队时间)
  void upsert_member_cache(rpc::context& ctx, const ::atfw::team::DTeamMember& member_data);

  // 维护待处理邀请/加入请求(key 无效或已过期则忽略)。同 key 且过期时间不变时原位覆盖，避免重排
  void upsert_pending_invitation(rpc::context& ctx, const ::atfw::team::DTeamInvitation& invitation);
  void remove_pending_invitation(const PROJECT_NAMESPACE_ID::DUserIDKey& invitee);
  void upsert_pending_join_request(rpc::context& ctx, const ::atfw::team::DTeamJoinRequest& join_request);
  void remove_pending_join_request(const PROJECT_NAMESPACE_ID::DUserIDKey& requester);

  // 队伍销毁/退出后清空本地缓存状态
  void reset_cached_state(rpc::context& ctx);

  void load_snapshot(rpc::context& ctx);

  void on_receive_raw_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& data,
                              bool refresh_member_data = true);

  void set_matching(rpc::context& ctx, bool value);

  void do_team_shared_data(rpc::context& ctx,
                           const ::google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& data);

  void do_member_shared_data(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                             const ::google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& data);

 private:
  friend class user_team_utility;

  user_team_manager* ATFW_UTIL_MACRO_NONNULL owner_;
  // 原 pending_dirty_snapshot_/dirty_remove_sent_/client_announced_/index_active_/is_member_/is_matching_
  // 六个布尔状态的合并存储, 语义见 team_flag 枚举值上的注释; 默认构造即全 false
  std::bitset<static_cast<size_t>(team_flag::kMax)> flags_;
  std::list<PROJECT_NAMESPACE_ID::DUserTeamDirty::TeamAction> pending_dirty_actions_;

  uint32_t team_type_;
  atfw::team::DTeamKey team_key_;
  atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t> channel_subscriber_;
  int64_t channel_create_sequence_;
  int64_t channel_saved_sequence_;

  std::chrono::system_clock::time_point actived_timepoint_;
  std::chrono::system_clock::time_point last_exit_team_request_timepoint_;
  std::chrono::system_clock::time_point exit_started_timepoint_;
  atfw::team::EnTeamExitReason last_exit_team_reason_;

  PROJECT_NAMESPACE_ID::DUserIDKey cached_captain_user_key_;
  atfw::team::EnTeamPermissionRole cached_permission_role_;
  atfw::team::DTeamConfigure cached_configure_;

  // 队伍状态缓存(快照 + 频道增量事件维护)，dump 快照时成员共享数据转出成解包后的模块数据
  // (DUserTeamSnapshot.unpacked_member_data)，不下发内部路由字段
  struct member_cache_data {
    // shared_member_data 字段在内存中恒为空(upsert 时逐字段拷贝、不复制原始打包数据)，
    // 共享成员数据由下方的 key-value 索引维护
    atfw::team::DTeamMember member_data;
    // 成员共享数据(解包后的模块数据，key 算法见 user_team_algorithm::make_team_member_shared_data_key)
    std::unordered_map<int64_t, PROJECT_NAMESPACE_ID::DTeamMemberSharedDataModule> shared_member_data;
  };
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, member_cache_data, user_key_hash_t, user_key_equal_t>
      cached_members_;
  // 队伍共享数据(解包后的模块数据，key 算法见 user_team_algorithm::make_team_shared_data_key)
  std::unordered_map<int64_t, PROJECT_NAMESPACE_ID::DTeamSharedDataModule> cached_team_shared_data_;
  // 队伍待处理的邀请和加入请求，与 user_team_manager 的 pending_join_request/pending_invitation 相同的双重索引:
  // 列表按过期时间升序(有效期长度大多一致，新条目通常直接追加到尾部)，索引表保存迭代器保证 O(1) 删除;
  // 过期条目由 cleanup_expired_admissions 清理(遇到第一个未过期条目即停止)
  using pending_invitation_list_t = std::list<atfw::team::DTeamInvitation>;
  using pending_join_request_list_t = std::list<atfw::team::DTeamJoinRequest>;
  pending_invitation_list_t pending_invitation_by_expired_time_;
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, pending_invitation_list_t::iterator, user_key_hash_t,
                     user_key_equal_t>
      pending_invitation_by_invitee_;
  pending_join_request_list_t pending_join_request_by_expired_time_;
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, pending_join_request_list_t::iterator, user_key_hash_t,
                     user_key_equal_t>
      pending_join_request_by_requester_;

  // 本端已确认的最新日志序号(随心跳上报，用于 teamsvr-room 记录成员 ack)
  int64_t last_applied_action_sequence_;
  uint64_t last_applied_action_hash_code_;
  std::chrono::system_clock::time_point last_heartbeat_timepoint_;
};
