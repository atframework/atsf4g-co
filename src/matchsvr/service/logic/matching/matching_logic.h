// Copyright 2026 atframework

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/pbdesc/com.struct.match.pb.h>
#include <protocol/pbdesc/match_service.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel_config_matching_index.h>
#include <config/server_frame_build_feature.h>
#include <data/user_key_hash_helper.h>

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class matching_room;

// 每次调用都从 server_frame 配置管理器读取当前配置组。
class matching_logic {
 public:
  using unit_view = std::vector<const PROJECT_NAMESPACE_ID::DMatchingUnit*>;
  using matching_rule_ptr =
      ::excel::excel_config_type_traits::shared_ptr<const PROJECT_NAMESPACE_ID::config::ExcelMatchingRule>;
  using matching_rule_list = std::vector<matching_rule_ptr>;
  using faction_assignment_list = google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>;

  struct faction_layout {
    size_t pending_user_count = 0;
    std::unordered_map<size_t, size_t> faction_count_by_capacity;
    std::set<size_t> fill_enabled_faction_capacities;
  };

  struct placement_candidate {
    int position = -1;
    bool completes_faction = false;
    size_t remaining_after_join = 0;

    // 判断候选是否指向一个有效的 faction assignment。
    bool is_valid() const noexcept;
    // 按补满优先、剩余容量最小、位置最前的顺序判断当前候选是否更优。
    bool is_better_than(const placement_candidate& other) const noexcept;
  };

  struct faction_join_progress {
    bool has_faction = false;
    bool joins_existing = false;
    bool completes_faction = false;
    size_t remaining_user_count = 0;
  };

  // 一次加入检查的完整结果；协议结果和放置进度始终来自同一次计算。
  struct join_check_result {
    PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation evaluation;
    faction_join_progress progress;
  };

  static constexpr const int64_t kDefaultSearchTimeout = 120;
  static constexpr const int64_t kDefaultConfirmTimeout = 15;
  static constexpr const int64_t kTerminalRetention = 60;
  static constexpr const int64_t kUnitHeartbeatTimeout = 10;
  static constexpr const size_t kMaxRebalanceMigrationsPerTarget = 8;
  static constexpr const size_t kMaxRebalanceMigrationsPerTick = 64;

  // 读取匹配池搜索超时，配置缺失或无效时使用服务默认值。
  static int64_t get_search_timeout_seconds(int32_t matching_pool_id);
  // 读取匹配池确认超时，配置缺失或无效时使用服务默认值。
  static int64_t get_confirm_timeout_seconds(int32_t matching_pool_id);

  static int64_t get_terminal_retention_seconds();
  static int64_t get_unit_heartbeat_timeout_seconds();
  static size_t get_max_rebalance_migrations_per_target();
  static size_t get_max_rebalance_migrations_per_tick();

  // 构造 Unit 加入房间的 WAL 事件。
  static PROJECT_NAMESPACE_ID::DMatchingEventLog make_add_unit_event(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 构造 Unit 离开房间的 WAL 事件，可携带迁移目标房间 ID。
  static PROJECT_NAMESPACE_ID::DMatchingEventLog make_remove_unit_event(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                                                                        const std::string& target_matching_id = {});
  // 按 matching_id 一致性哈希选择负责创建战斗的 orbitsvr。
  static uint64_t get_orbitsvr_server_id(const std::string& match_id);

  // 校验 unit 自身是否合法以及是否超过匹配池的组队上限。
  static int32_t validate_unit(int32_t matching_pool_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 新房只有一个 Unit，直接用模板容量索引选择合法 faction，不执行多 Unit 完整求解。
  static join_check_result check_unit_can_create_room(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                                      const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
                                                      int32_t global_matching_users_count);
  // 只检查 unit 能否作为一个整体加入指定房间，并返回同次计算产生的 faction 补位进度。
  static join_check_result check_unit_can_join(const matching_room& room,
                                               const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
                                               int32_t global_matching_users_count);
  // Rebalance 单 Unit 只允许补已有 faction，或加入不使用 faction 的房间，不创建新的 faction。
  static join_check_result check_unit_can_join_for_rebalance(const matching_room& room,
                                                             const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                                                             int64_t now, int32_t global_matching_users_count);
  // 把一个已经满员的 faction 作为整体加入目标房间，保留其容量和 Unit 成员关系并返回补位进度。
  static join_check_result check_faction_can_join(const matching_room& room, const unit_view& faction_units,
                                                  uint32_t faction_capacity, int64_t now,
                                                  int32_t global_matching_users_count);
  // 只检查房间当前已有的 unit 是否满足成局条件，不尝试加入新 unit。
  static PROJECT_NAMESPACE_ID::DMatchingRoomReadyEvaluation check_room_ready(const matching_room& room, int64_t now,
                                                                             int32_t global_matching_users_count);

 private:
  using user_key_set = std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t>;

  // 构造 Unit 成员键集合，用于屏蔽关系的常数复杂度查询。
  static user_key_set make_unit_user_key_set(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit);
  // 判断用户键列表中是否存在目标 Unit 的成员。
  static bool contains_any_unit_user(
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DUserIDKey>& candidates,
      const user_key_set& unit_users);
  // 判断 Unit 级屏蔽、lasting 屏蔽或上一场玩家中是否包含目标 Unit 的成员。
  static bool unit_bans_any_user(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, const user_key_set& target_users);
  // 判断两个不可拆分 Unit 之间是否存在显式或历史屏蔽关系。
  static bool units_are_banned(const PROJECT_NAMESPACE_ID::DMatchingUnit& left,
                               const PROJECT_NAMESPACE_ID::DMatchingUnit& right);
  // 计算两个 32 位整数的绝对差，使用 64 位中间值避免减法溢出。
  static int32_t abs_diff(int32_t left, int32_t right);
  // 判断一条规则项是否允许两个 Unit 的匹配参数共存。
  static bool matches_rule_item(const PROJECT_NAMESPACE_ID::config::DMatchingRuleItem& item,
                                const PROJECT_NAMESPACE_ID::DMatchingParameter& left,
                                const PROJECT_NAMESPACE_ID::DMatchingParameter& right);
  // 根据匹配池、等待时长和全局人数选择当前生效的规则。
  static matching_rule_list select_rules(const PROJECT_NAMESPACE_ID::DMatchingScope& scope, int64_t created_time,
                                         int64_t now, int32_t global_matching_users_count);
  // 检查待加入 Unit 与房间已有 Unit 是否满足屏蔽和数值规则。
  static bool check_incoming_units_matching_rule_limit(const unit_view& existing_units, const unit_view& incoming_units,
                                                       const matching_rule_ptr& rule);
  // 检查合并后的 force_type 数量和 region 是否满足规则限制。
  static bool check_rule_limits(const PROJECT_NAMESPACE_ID::DMatchingScope& scope, const unit_view& existing_units,
                                const unit_view& incoming_units, const matching_rule_ptr& rule);
  // 检查一条已通过模板预筛选的规则是否满足合并后的公共限制和待加入 Unit 约束。
  static bool check_rule_compatibility(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                       const unit_view& existing_units, const unit_view& incoming_units,
                                       const matching_rule_ptr& rule);
  // 深拷贝 faction 分配列表，避免调用方共享 protobuf 元素所有权。
  static faction_assignment_list copy_assignments(const faction_assignment_list& source);
  // 从房间缓存的 faction 统计信息构造模板匹配所需布局。
  static faction_layout get_faction_layout(const matching_room& room);
  // 判断结果模板是否仍有容量容纳当前 faction 布局。
  static bool template_contains_layout(const faction_layout& layout,
                                       const excel::matching_result_template_index_t& result_template);
  // 检查结果模板的总人数和 faction 容量是否可以容纳当前候选布局。
  static bool template_can_contain_layout(const faction_layout& layout, size_t total_users,
                                          const excel::matching_result_template_index_t& result_template);
  // 判断结果模板是否与当前已填满的 faction 布局完全一致。
  static bool template_exactly_matches_layout(const faction_layout& layout,
                                              const excel::matching_result_template_index_t& result_template);
  // 检查已有候选关卡与所有待加入 Unit 是否仍存在非空交集。
  static bool incoming_levels_are_compatible(const std::vector<int32_t>& existing_level_ids,
                                             const unit_view& incoming_units);
  // 检查不使用 faction 的规则是否允许当前 Unit 布局。
  static bool has_compatible_rule_without_factions(const matching_rule_list& rules,
                                                   const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                                   const unit_view& existing_units, const unit_view& incoming_units,
                                                   const faction_layout& current_layout);
  // 检查至少一个 faction 结果模板是否可以容纳当前布局并满足对应规则。
  static bool has_compatible_faction_layout(const matching_rule_list& rules,
                                            const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                            const unit_view& existing_units, const unit_view& incoming_units,
                                            size_t total_users, const faction_layout& current_layout);
  // 计算当前 Unit 新建 faction 时所有合法模板允许的最大容量，返回 0 表示没有合法方案。
  static size_t get_max_new_faction_capacity(const matching_rule_list& rules,
                                             const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                             const unit_view& existing_units, size_t total_users,
                                             const faction_layout& current_layout,
                                             const PROJECT_NAMESPACE_ID::DMatchingUnit& new_faction_unit);
  // 取得 assignments 所有权并构造成功加入结果，避免候选成功后再次复制全部 membership。
  static join_check_result make_join_evaluation(faction_assignment_list assignments, faction_join_progress progress);
  // 构造指定错误码的拒绝加入结果。
  static join_check_result make_rejected_join(int32_t result_code);
  // 公共 Unit 加入实现；allow_new_faction=false 时只允许补已有 faction 或使用无 faction 模板。
  static join_check_result check_unit_can_join_impl(const matching_room& room,
                                                    const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
                                                    int32_t global_matching_users_count, bool allow_new_faction);
};
