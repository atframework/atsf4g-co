// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.struct.match.pb.h>
#include <protocol/pbdesc/com.struct.orbit.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <data/user_key_hash_helper.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "logic/matching/matching_wal_handle.h"

namespace rpc {
class context;
}

// 一次匹配房间。房间只管理不可拆分的组队 unit 和自身状态，不负责选择配置或全局索引。
class matching_room {
 public:
  using ptr_t = std::shared_ptr<matching_room>;

  struct faction_statistics {
    std::unordered_map<size_t, size_t> faction_count_by_capacity;
    std::set<size_t> fill_enabled_faction_capacities;
    // 与 assignments 位置对齐的实际人数，用于提交时规范化 protobuf 缓存。
    std::vector<uint32_t> assigned_user_counts;
    size_t completed_faction_count = 0;
    size_t pending_user_count = 0;
  };

  // 一个订阅者迁移所需的路由和已确认 WAL 游标。
  struct subscriber_route {
    uint64_t server_id = 0;
    int64_t acknowledge_event_id = 0;
  };

  // 使用已确定的粗桶和初始关卡创建一个空房间。
  matching_room(std::string matching_id, const PROJECT_NAMESPACE_ID::DMatchingScope& scope, int32_t selected_level_id,
                int64_t now, int64_t expire_time);

  // 返回房间的稳定 ID。
  const std::string& get_matching_id() const noexcept { return matching_id_; }
  // 返回 level_type/region/version/pool 四维粗粒度匹配桶。
  const PROJECT_NAMESPACE_ID::DMatchingScope& get_scope() const noexcept { return scope_; }
  // 返回全部 Unit 当前仍共同接受的关卡集合。
  const std::vector<int32_t>& get_compatible_level_ids() const noexcept { return compatible_level_ids_; }
  // 返回房间当前从候选交集中选中的最终关卡。
  int32_t get_selected_level_id() const noexcept { return selected_level_id_; }
  // 返回当前房间状态。
  PROJECT_NAMESPACE_ID::EnMatchingRoomStatus get_status() const noexcept { return status_; }
  // 返回房间创建时间，候选房间按它从旧到新排序。
  int64_t get_created_time() const noexcept { return created_time_; }
  // 返回搜索超时时间。
  int64_t get_expire_time() const noexcept { return expire_time_; }
  // 返回终态发生时间，用于延迟回收以支持查询。
  int64_t get_terminal_time() const noexcept { return terminal_time_; }
  // 返回确认阶段截止时间；非确认阶段为 0。
  int64_t get_confirm_expire_time() const noexcept { return confirm_expire_time_; }
  // 返回创建战斗阶段截止时间；非创建阶段为 0。
  int64_t get_battle_create_expire_time() const noexcept { return battle_create_expire_time_; }
  // 返回房间 WAL 最近分配的事件 ID。
  int64_t get_last_event_id() const noexcept { return last_event_id_; }
  // 返回成局时动态选中的最终结果模板 ID；搜索期间为 0。
  int32_t get_result_template_id() const noexcept { return result_template_id_; }
  // 返回房间当前业务结果。
  int32_t get_result() const noexcept { return result_; }
  // 返回当前所有 unit，key 为 unit_id。
  const std::unordered_map<uint64_t, PROJECT_NAMESPACE_ID::DMatchingUnit>& get_units() const noexcept { return units_; }
  const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>& get_faction_assignments()
      const noexcept {
    return faction_assignments_;
  }
  const std::unordered_map<size_t, size_t>& get_faction_count_by_capacity() const noexcept {
    return faction_count_by_capacity_;
  }
  const std::set<size_t>& get_fill_enabled_faction_capacities() const noexcept {
    return fill_enabled_faction_capacities_;
  }
  size_t get_completed_faction_count() const noexcept { return completed_faction_count_; }
  size_t get_pending_faction_user_count() const noexcept { return pending_faction_user_count_; }

  const PROJECT_NAMESPACE_ID::DMatchingOrbitInitData& get_match_orbit_user_init_detail(
      const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const;

  void add_orbit_user_init_detail(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                  const PROJECT_NAMESPACE_ID::DOrbitUserInitDataDetail& detail,
                                  const std::string& user_open_id);

  const PROJECT_NAMESPACE_ID::DOrbitRoomKey& get_orbit_room_key() const noexcept { return orbit_room_key_; }

  // 统计房间内的真实玩家数量。
  size_t get_user_count() const noexcept { return user_count_; }
  // 返回按 Unit 人数索引的 Unit 数量，用于匹配模板快速判定。
  const std::vector<size_t>& get_unit_size_counts() const noexcept { return unit_size_counts_; }
  // 判断 unit 是否仍在本房间。
  bool has_unit(uint64_t unit_id) const noexcept;
  // 查找房间内的 Unit；不存在时返回 nullptr。
  const PROJECT_NAMESPACE_ID::DMatchingUnit* find_unit(uint64_t unit_id) const noexcept;
  // 判断玩家是否仍在本房间。
  bool has_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) const noexcept;

  // 原子加入一个不可拆分 unit；重复 unit 或玩家返回 false。
  bool add_unit(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 从仍在匹配的房间移除 unit；返回是否实际移除。
  bool remove_unit(uint64_t unit_id);
  // 撮合完成后进入战斗确认，并把全部成员重置为待确认。
  void begin_confirmation(int64_t expire_time) noexcept;
  // 更新单个成员的确认选择；只有确认阶段且成员存在时成功。
  bool confirm_user(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool accepted) noexcept;
  // 判断房间内所有成员是否均已接受。
  bool are_all_users_confirmed() const noexcept;
  // 确认失败移除 Unit 后，让剩余 Unit 回到正常撮合。
  void resume_matching(int64_t expire_time) noexcept;
  // 保存成局时动态选中的最终结果模板；继续搜索时清零。
  void set_result_template_id(int32_t value) noexcept;
  // 原子校验并保存搜索阶段的临时 faction 分配；结构不合法时返回 false 且保持原值。
  bool set_faction_assignments(
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>& value);
  void clear_faction_assignments() noexcept;
  // 进入创建战斗前按稳定 membership 顺序一次性生成最终 faction_id；重复调用保持原映射。
  bool finalize_faction_ids();
  int32_t get_unit_faction_id(uint64_t unit_id) const noexcept;
  // 有新 unit 加入时延长房间级搜索截止时间，避免迁入老房间后立即超时。
  void extend_expire_time(int64_t value) noexcept;
  // 标记正在请求 battlesvr，之后不再接受新 unit。
  void mark_creating_battle(uint64_t orbit_server_id, int64_t expire_time) noexcept;
  uint64_t get_orbit_server_id() const noexcept;
  bool is_orbit_ready_processing() const noexcept { return orbit_ready_processing_; }
  // 锁定首次 orbitsvr ready 回调，防止重复初始化玩家。
  bool begin_orbit_ready(uint64_t source_server_id) noexcept;
  // 标记战斗房间创建完成。
  void mark_finished(int64_t now);
  // 标记战斗请求失败。
  void mark_failed(int32_t result, int64_t now) noexcept;
  // 标记搜索超时。
  void mark_timeout(int64_t now) noexcept;
  // 标记所有 unit 已取消。
  void mark_cancelled(int64_t now) noexcept;
  // 导出不暴露内部容器的协议快照。
  void dump(PROJECT_NAMESPACE_ID::DMatchingRoomSnapshot& output) const;
  // 导出指定 Unit 的玩家视图，也用于刚移除但仍需接收最终事件的 Unit。
  void dump_player_view(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                        PROJECT_NAMESPACE_ID::DMatchingPlayerView& output) const;
  void set_orbit_expired_timepoint(int64_t value) noexcept { orbit_expired_timepoint_ = value; }

  // 为玩家创建或刷新 WAL 订阅；acknowledge_event_id 用于增量重放。
  bool subscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, uint64_t server_id,
                 int64_t acknowledge_event_id);
  // 主动移除玩家订阅。
  void unsubscribe(rpc::context& ctx, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);
  // 读取订阅路由，供迁房时把订阅者转移到目标房间；不存在时返回空值。
  std::optional<subscriber_route> get_subscriber_route(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key);
  // 追加并立即广播一条房间 WAL 日志。
  void publish(rpc::context& ctx, PROJECT_NAMESPACE_ID::DMatchingEventLog&& event_log);

 private:
  // 校验 faction 分配覆盖关系并一次性计算房间缓存的 faction 统计信息。
  static std::optional<faction_statistics> calculate_faction_statistics(
      const std::unordered_map<uint64_t, PROJECT_NAMESPACE_ID::DMatchingUnit>& units,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>& assignments);
  // 按最小 Unit ID 稳定排列已校验且非空的 faction assignment。
  static bool faction_assignment_precedes(const PROJECT_NAMESPACE_ID::DMatchingFactionAssignment* left,
                                          const PROJECT_NAMESPACE_ID::DMatchingFactionAssignment* right) noexcept;
  // 原子提交已校验的 assignments，并把实际人数写回每个 faction 的冗余缓存。
  void commit_faction_assignments(
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>&& assignments,
      faction_statistics&& statistics);

  // 匹配房间的稳定 ID。
  std::string matching_id_;
  // 四维粗粒度匹配桶。
  PROJECT_NAMESPACE_ID::DMatchingScope scope_;
  // 全部 Unit 可选关卡的交集，始终有序且去重。
  std::vector<int32_t> compatible_level_ids_;
  // 当前从 compatible_level_ids_ 中选中的关卡。
  int32_t selected_level_id_;
  // 房间当前状态。
  PROJECT_NAMESPACE_ID::EnMatchingRoomStatus status_;
  // unit_id 到完整组队数据的映射。
  std::unordered_map<uint64_t, PROJECT_NAMESPACE_ID::DMatchingUnit> units_;
  // 随 Unit 增删增量维护，避免高频成局检查重复遍历房间。
  size_t user_count_;
  std::vector<size_t> unit_size_counts_;
  // 搜索阶段持续维护 membership/capacity；进入确认后锁定成员关系，但尚不生成 faction_id。
  google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment> faction_assignments_;
  std::unordered_map<size_t, size_t> faction_count_by_capacity_;
  std::set<size_t> fill_enabled_faction_capacities_;
  size_t completed_faction_count_;
  size_t pending_faction_user_count_;
  // 仅在确认完成、准备创建战斗时固化；匹配阶段不维护最终 faction_id。
  std::unordered_map<uint64_t, int32_t> finalized_unit_faction_ids_;
  bool faction_ids_finalized_;

  // 创建时间，同时作为老房间优先的排序时间。
  int64_t created_time_;
  // 匹配搜索截止时间。
  int64_t expire_time_;
  // 进入终态的时间，非终态时为 0。
  int64_t terminal_time_;
  // 确认阶段截止时间。
  int64_t confirm_expire_time_;
  // 创建战斗阶段截止时间。
  int64_t battle_create_expire_time_ = 0;
  // 单房间单调递增 WAL 事件 ID。
  int64_t last_event_id_;
  // 成局时动态选中的最终结果模板 ID，搜索期间不锁定模板。
  int32_t result_template_id_;
  // 房间最终业务结果。
  int32_t result_;
  // 是否已经开始处理 orbitsvr ready 回调。
  bool orbit_ready_processing_;
  // 本房间创建请求选中的 orbitsvr，用于校验 ready 回调来源及通知 lobbysvr。
  uint64_t orbit_server_id_;

  // OrbitRoom Key
  PROJECT_NAMESPACE_ID::DOrbitRoomKey orbit_room_key_;
  int64_t orbit_expired_timepoint_ = 0;

  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, PROJECT_NAMESPACE_ID::DMatchingOrbitInitData, user_key_hash_t,
                     user_key_equal_t>
      orbit_users_init_detail_;
  // 房间级 WAL publisher，负责日志保留、重放、快照和跨服通知。
  matching_wal_log_operator::strong_ptr<matching_wal_publisher> wal_publisher_;
};
