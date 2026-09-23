// Copyright 2026 atframework

#include "logic/matching/matching_logic.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>
#include <protocol/config/match_config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel/config_easy_api.h>
#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "config/excel_config_matching_index.h"
#include "logic/logic_server_setup.h"
#include "logic/matching/matching_room.h"
#include "logic/matching/matching_unit.h"
#include "logic/matching/matching_utility.h"

#if !((defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
constexpr const int64_t matching_logic::kDefaultSearchTimeout;
constexpr const int64_t matching_logic::kDefaultConfirmTimeout;
constexpr const int64_t matching_logic::kTerminalRetention;
constexpr const int64_t matching_logic::kUnitHeartbeatTimeout;
constexpr const size_t matching_logic::kMaxRebalanceMigrationsPerTarget;
constexpr const size_t matching_logic::kMaxRebalanceMigrationsPerTick;
#endif

bool matching_logic::placement_candidate::is_valid() const noexcept { return position >= 0; }

bool matching_logic::placement_candidate::is_better_for_priority_than(const placement_candidate& other) const noexcept {
  if (completes_faction != other.completes_faction) {
    return completes_faction;
  }
  if (remaining_after_join != other.remaining_after_join) {
    return remaining_after_join < other.remaining_after_join;
  }
  return position < other.position;
}

bool matching_logic::placement_candidate::is_better_for_balance_than(const placement_candidate& other) const noexcept {
  if (user_count_spread_after_join != other.user_count_spread_after_join) {
    return user_count_spread_after_join < other.user_count_spread_after_join;
  }
  return position < other.position;
}

int64_t matching_logic::get_search_timeout_seconds(int32_t matching_pool_id) {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::matchsvr_cfg>();
  int64_t default_search_timeout = server_cfg.default_search_timeout();
  if (default_search_timeout <= 0) {
    default_search_timeout = kDefaultSearchTimeout;
  }

  auto pool = excel::get_ExcelMatchingPool_by_id(matching_pool_id);
  return pool && pool->search_timeout_seconds() > 0 ? pool->search_timeout_seconds() : default_search_timeout;
}

int64_t matching_logic::get_confirm_timeout_seconds(int32_t matching_pool_id) {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::matchsvr_cfg>();
  int64_t default_confirm_timeout = server_cfg.default_confirm_timeout();
  if (default_confirm_timeout <= 0) {
    default_confirm_timeout = kDefaultConfirmTimeout;
  }
  auto pool = excel::get_ExcelMatchingPool_by_id(matching_pool_id);
  return pool && pool->confirm_timeout_seconds() > 0 ? pool->confirm_timeout_seconds() : default_confirm_timeout;
}

int64_t matching_logic::get_terminal_retention_seconds() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::matchsvr_cfg>();
  int64_t default_terminal_retention = server_cfg.terminal_retention();
  if (default_terminal_retention <= 0) {
    default_terminal_retention = kTerminalRetention;
  }
  return default_terminal_retention;
}

int64_t matching_logic::get_unit_heartbeat_timeout_seconds() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::matchsvr_cfg>();
  int64_t default_heartbeat_timeout = server_cfg.unit_heartbeat_timeout();
  if (default_heartbeat_timeout <= 0) {
    default_heartbeat_timeout = kUnitHeartbeatTimeout;
  }
  return default_heartbeat_timeout;
}

size_t matching_logic::get_max_rebalance_migrations_per_target() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::matchsvr_cfg>();
  size_t default_max_migrations_per_target = server_cfg.max_rebalance_migrations_per_target();
  if (default_max_migrations_per_target <= 0) {
    default_max_migrations_per_target = kMaxRebalanceMigrationsPerTarget;
  }
  return default_max_migrations_per_target;
}

size_t matching_logic::get_max_rebalance_migrations_per_tick() {
  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::matchsvr_cfg>();
  size_t default_max_migrations_per_tick = server_cfg.max_rebalance_migrations_per_tick();
  if (default_max_migrations_per_tick <= 0) {
    default_max_migrations_per_tick = kMaxRebalanceMigrationsPerTick;
  }
  return default_max_migrations_per_tick;
}

PROJECT_NAMESPACE_ID::DMatchingEventLog matching_logic::make_add_unit_event(
    const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  PROJECT_NAMESPACE_ID::DMatchingEventLog result;
  protobuf_copy_message(*result.mutable_add_unit(), unit);
  return result;
}

PROJECT_NAMESPACE_ID::DMatchingEventLog matching_logic::make_remove_unit_event(
    const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, const std::string& target_matching_id) {
  PROJECT_NAMESPACE_ID::DMatchingEventLog result;
  protobuf_copy_message(*result.mutable_remove_unit()->mutable_unit(), unit);

  result.mutable_remove_unit()->set_switch_to_matching_id(target_matching_id);
  return result;
}

uint64_t matching_logic::get_orbitsvr_server_id(const std::string& match_id) {
  auto* module = logic_server_last_common_module();
  if (module == nullptr) {
    return 0;
  }
  auto discovery =
      module->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kOrbitSvr));
  if (!discovery) {
    return 0;
  }

  auto selected = discovery->get_node_by_consistent_hash(match_id);
  if (!selected) {
    return 0;
  }
  return selected->get_discovery_info().id();
}

matching_logic::user_key_set matching_logic::make_unit_user_key_set(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  user_key_set result;
  result.reserve(static_cast<size_t>(unit.users_size()));
  for (const auto& user : unit.users()) {
    result.emplace(user.user_key());
  }
  return result;
}

bool matching_logic::contains_any_unit_user(
    const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DUserIDKey>& candidates,
    const user_key_set& unit_users) {
  for (const auto& candidate : candidates) {
    if (unit_users.find(candidate) != unit_users.end()) {
      return true;
    }
  }
  return false;
}

bool matching_logic::unit_bans_any_user(const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                                        const user_key_set& target_users) {
  if (contains_any_unit_user(unit.ban_users(), target_users)) {
    return true;
  }
  for (const auto& user : unit.users()) {
    if (contains_any_unit_user(user.lasting_ban_users(), target_users) ||
        contains_any_unit_user(user.last_battle_users(), target_users)) {
      return true;
    }
  }
  return false;
}

bool matching_logic::units_are_banned(const PROJECT_NAMESPACE_ID::DMatchingUnit& left,
                                      const PROJECT_NAMESPACE_ID::DMatchingUnit& right) {
  const auto left_users = make_unit_user_key_set(left);
  if (unit_bans_any_user(right, left_users)) {
    return true;
  }
  return unit_bans_any_user(left, make_unit_user_key_set(right));
}

int32_t matching_logic::abs_diff(int32_t left, int32_t right) {
  int64_t result = static_cast<int64_t>(left) - static_cast<int64_t>(right);
  return static_cast<int32_t>(result < 0 ? -result : result);
}

bool matching_logic::matches_rule_item(const PROJECT_NAMESPACE_ID::config::DMatchingRuleItem& item,
                                       const PROJECT_NAMESPACE_ID::DMatchingParameter& left,
                                       const PROJECT_NAMESPACE_ID::DMatchingParameter& right) {
  if (item.values_size() == 0) {
    return true;
  }
  switch (item.type()) {
    case PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF:
      return std::all_of(item.values().begin(), item.values().end(),
                         [&](int32_t limit) { return abs_diff(left.rank_level(), right.rank_level()) <= limit; });
    case PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_ROLE_LEVEL_DIFF:
      return std::all_of(item.values().begin(), item.values().end(),
                         [&](int32_t limit) { return abs_diff(left.role_level(), right.role_level()) <= limit; });
    default:
      return true;
  }
}

matching_logic::matching_rule_list matching_logic::select_rules(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                                                int64_t created_time, int64_t now,
                                                                int32_t global_matching_users_count) {
  matching_rule_list result;
  auto pool = excel::get_ExcelMatchingPool_by_id(scope.matching_pool_id());
  if (!pool) {
    return result;
  }
  for (int32_t group_id : pool->rule_group_ids()) {
    auto group = excel::get_ExcelMatchingRuleGroup_by_group_id(group_id);
    if (!group || global_matching_users_count < group->global_user_lower() ||
        (group->global_user_upper() > 0 && global_matching_users_count >= group->global_user_upper())) {
      continue;
    }
    const int64_t elapsed = std::max<int64_t>(0, now - created_time);
    for (int32_t rule_id : group->pool_rules()) {
      auto rule = excel::get_ExcelMatchingRule_by_id(rule_id);
      if (!rule || elapsed < rule->time_limit().min() ||
          (rule->time_limit().max() > 0 && elapsed > rule->time_limit().max())) {
        continue;
      }
      result.emplace_back(rule);
    }
    return result;
  }
  return result;
}

bool matching_logic::check_incoming_units_compatibility(const unit_view& existing_units,
                                                        const unit_view& incoming_units) {
  for (const auto* existing : existing_units) {
    if (existing == nullptr) {
      return false;
    }
  }
  for (const auto* incoming : incoming_units) {
    if (incoming == nullptr) {
      return false;
    }
    for (const auto* existing : existing_units) {
      if (units_are_banned(*existing, *incoming)) {
        return false;
      }
    }
  }
  return true;
}

bool matching_logic::check_rule_compatibility(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                              const unit_view& existing_units, const unit_view& incoming_units,
                                              const matching_rule_ptr& rule) {
  if (rule->region_limits_size() > 0 && std::find(rule->region_limits().begin(), rule->region_limits().end(),
                                                  scope.region()) == rule->region_limits().end()) {
    return false;
  }
  if (rule->rules().empty()) {
    return true;
  }
  for (const auto* incoming : incoming_units) {
    for (const auto* existing : existing_units) {
      for (const auto& item : rule->rules()) {
        if (!matches_rule_item(item, existing->parameter(), incoming->parameter())) {
          return false;
        }
      }
    }
  }
  return true;
}

matching_logic::faction_assignment_list matching_logic::copy_assignments(const faction_assignment_list& source) {
  faction_assignment_list output;
  for (const auto& assignment : source) {
    protobuf_copy_message(*output.Add(), assignment);
  }
  return output;
}

size_t matching_logic::get_faction_user_count_spread(const faction_assignment_list& assignments) {
  if (assignments.size() <= 1) {
    return 0;
  }
  size_t minimum_user_count = static_cast<size_t>(assignments.Get(0).assigned_user_count());
  size_t maximum_user_count = minimum_user_count;
  for (int position = 1; position < assignments.size(); ++position) {
    const size_t assigned_user_count = static_cast<size_t>(assignments.Get(position).assigned_user_count());
    minimum_user_count = std::min(minimum_user_count, assigned_user_count);
    maximum_user_count = std::max(maximum_user_count, assigned_user_count);
  }
  return maximum_user_count - minimum_user_count;
}

matching_logic::placement_candidate matching_logic::find_best_existing_faction(
    const faction_assignment_list& assignments, size_t unit_size, size_t fill_capacity, bool balanced) {
  placement_candidate best_placement;
  size_t minimum = std::numeric_limits<size_t>::max();
  size_t second_minimum = minimum;
  size_t maximum = 0;
  for (const auto& assignment : assignments) {
    const size_t count = assignment.assigned_user_count();
    if (count < minimum) {
      second_minimum = minimum;
      minimum = count;
    } else if (count < second_minimum) {
      second_minimum = count;
    }
    maximum = std::max(maximum, count);
  }
  for (int position = 0; position < assignments.size(); ++position) {
    const auto& assignment = assignments.Get(position);
    const size_t joined_count = static_cast<size_t>(assignment.assigned_user_count()) + unit_size;
    if (assignment.user_capacity() != fill_capacity || joined_count > fill_capacity) {
      continue;
    }
    placement_candidate candidate;
    candidate.position = position;
    candidate.remaining_after_join = fill_capacity - joined_count;
    candidate.completes_faction = candidate.remaining_after_join == 0;
    const size_t other_minimum = assignment.assigned_user_count() == minimum ? second_minimum : minimum;
    candidate.user_count_spread_after_join = std::max(maximum, joined_count) - std::min(other_minimum, joined_count);
    if (!best_placement.is_valid() || (balanced ? candidate.is_better_for_balance_than(best_placement)
                                                : candidate.is_better_for_priority_than(best_placement))) {
      best_placement = candidate;
    }
  }
  return best_placement;
}

bool matching_logic::incoming_levels_are_compatible(const std::vector<int32_t>& existing_level_ids,
                                                    const unit_view& incoming_units) {
  std::vector<int32_t> compatible_level_ids = existing_level_ids;
  for (const auto* unit : incoming_units) {
    if (unit == nullptr || unit->acceptable_level_ids().empty()) {
      return false;
    }
    if (compatible_level_ids.empty()) {
      compatible_level_ids.assign(unit->acceptable_level_ids().begin(), unit->acceptable_level_ids().end());
      continue;
    }
    std::vector<int32_t> intersection;
    std::set_intersection(compatible_level_ids.begin(), compatible_level_ids.end(),
                          unit->acceptable_level_ids().begin(), unit->acceptable_level_ids().end(),
                          std::back_inserter(intersection));
    compatible_level_ids = std::move(intersection);
    if (compatible_level_ids.empty()) {
      return false;
    }
  }
  return !compatible_level_ids.empty();
}
size_t matching_logic::get_max_faction_capacity(const faction_assignment_list& assignments) {
  size_t result = 0;
  for (const auto& assignment : assignments) {
    result = std::max(result, static_cast<size_t>(assignment.user_capacity()));
  }
  return result;
}

bool matching_logic::has_compatible_rule(const matching_rule_list& rules,
                                         const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                         const unit_view& existing_units, const unit_view& incoming_units,
                                         size_t incoming_capacity, size_t maximum_existing_capacity) {
  if (rules.empty() || !check_incoming_units_compatibility(existing_units, incoming_units)) {
    return false;
  }
  for (const auto& rule : rules) {
    switch (rule->faction_add_rule()) {
      case PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_NONE:
        break;
      case PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_SAME_AS_TEAM:
        if (maximum_existing_capacity > 0 && incoming_capacity != maximum_existing_capacity) {
          continue;
        }
        break;
      default:
        continue;
    }
    if (check_rule_compatibility(scope, existing_units, incoming_units, rule)) {
      return true;
    }
  }
  return false;
}

matching_logic::join_check_result matching_logic::make_join_evaluation(faction_assignment_list assignments,
                                                                       faction_join_progress progress) {
  join_check_result result;
  result.evaluation.set_can_join(true);
  result.evaluation.set_result(0);
  result.evaluation.mutable_faction_assignments()->Swap(&assignments);
  result.progress = progress;
  return result;
}

matching_logic::join_check_result matching_logic::make_rejected_join(int32_t result_code) {
  join_check_result result;
  result.evaluation.set_result(result_code);
  return result;
}

int32_t matching_logic::validate_unit(int32_t matching_pool_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  auto pool = excel::get_ExcelMatchingPool_by_id(matching_pool_id);
  if (!pool) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND;
  }
  if (unit.unit_id() == 0 || unit.users_size() == 0 || pool->unit_max_size() <= 0 ||
      unit.users_size() > pool->unit_max_size() ||
      (unit.faction_fill_policy() != PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE &&
       unit.faction_fill_policy() != PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE)) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
  }
  for (int left = 0; left < unit.users_size(); ++left) {
    if (unit.users(left).user_key().user_id() == 0) {
      return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
    }
    for (int right = left + 1; right < unit.users_size(); ++right) {
      if (matching_utility::same_user(unit.users(left).user_key(), unit.users(right).user_key())) {
        return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
      }
    }
  }
  return 0;
}

matching_logic::join_check_result matching_logic::check_unit_can_create_room(
    const PROJECT_NAMESPACE_ID::DMatchingScope& scope, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
    int32_t global_matching_users_count) {
  auto pool = excel::get_ExcelMatchingPool_by_id(scope.matching_pool_id());
  if (!pool) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
  }
  const size_t unit_size = static_cast<size_t>(unit.users_size());
  if (unit_size == 0 || unit.acceptable_level_ids().empty() || pool->unit_max_size() <= 0 ||
      unit_size > static_cast<size_t>(pool->unit_max_size())) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
  }
  if (pool->max_user_cout_limit() > 0 && unit_size > static_cast<size_t>(pool->max_user_cout_limit())) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL);
  }

  unit_view existing_units;
  unit_view incoming_units{&unit};
  const auto rules = select_rules(scope, now, now, global_matching_users_count);
  faction_assignment_list assignments;
  if (pool->max_faction_cout_limit() <= 0) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  const bool fill_enabled = unit.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE;
  const size_t fill_capacity = excel::get_matching_pool_faction_capacity(pool->id());
  const size_t faction_capacity = fill_enabled ? fill_capacity : unit_size;
  if (fill_capacity == 0 || unit_size > fill_capacity ||
      !has_compatible_rule(rules, scope, existing_units, incoming_units, faction_capacity, 0)) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  auto* assignment = assignments.Add();
  assignment->set_user_capacity(static_cast<uint32_t>(faction_capacity));
  assignment->add_unit_ids(unit.unit_id());
  assignment->set_assigned_user_count(static_cast<uint32_t>(unit_size));
  faction_join_progress progress;
  progress.has_faction = true;
  progress.completes_faction = unit_size == faction_capacity;
  progress.remaining_user_count = faction_capacity - unit_size;
  progress.faction_count_after_join = 1;
  progress.faction_user_count_spread_after_join = 0;
  return make_join_evaluation(std::move(assignments), progress);
}

matching_logic::join_check_result matching_logic::check_unit_can_join(const matching_room& room,
                                                                      const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                                                                      int64_t now,
                                                                      int32_t global_matching_users_count) {
  return check_unit_can_join_impl(room, unit, now, global_matching_users_count, true);
}

matching_logic::join_check_result matching_logic::check_unit_can_join_for_rebalance(
    const matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
    int32_t global_matching_users_count) {
  return check_unit_can_join_impl(room, unit, now, global_matching_users_count, false);
}

matching_logic::join_check_result matching_logic::check_unit_can_join_impl(
    const matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
    int32_t global_matching_users_count, bool allow_new_faction) {
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
  }

  const size_t unit_size = static_cast<size_t>(unit.users_size());

  // 人数上限检查
  if (unit_size == 0 || pool->unit_max_size() <= 0 || unit_size > static_cast<size_t>(pool->unit_max_size())) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
  }
  if (pool->max_user_cout_limit() > 0 &&
      room.get_user_count() + unit_size > static_cast<size_t>(pool->max_user_cout_limit())) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL);
  }

  unit_view existing_units;
  existing_units.reserve(room.get_units().size());
  for (const auto& stored : room.get_units()) {
    if (!stored.second) {
      return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    }
    existing_units.emplace_back(&stored.second->get_data());
  }

  // 相同关卡
  unit_view incoming_units{&unit};
  if (!incoming_levels_are_compatible(room.get_compatible_level_ids(), incoming_units)) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_NOT_SAME_LEVEL);
  }

  const auto& assignments = room.get_faction_assignments();
  if (!assignments.empty() &&
      (pool->max_faction_cout_limit() <= 0 || assignments.size() > pool->max_faction_cout_limit())) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL);
  }
  const bool unit_can_fill_faction =
      unit.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE;
  const size_t fill_capacity = excel::get_matching_pool_faction_capacity(pool->id());
  if (fill_capacity == 0 || unit_size > fill_capacity || pool->max_faction_cout_limit() <= 0) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  const size_t incoming_capacity = unit_can_fill_faction ? fill_capacity : unit_size;
  const auto rules = select_rules(room.get_scope(), room.get_created_time(), now, global_matching_users_count);
  if (!has_compatible_rule(rules, room.get_scope(), existing_units, incoming_units, incoming_capacity,
                           get_max_faction_capacity(assignments))) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  const bool can_create_faction = allow_new_faction && assignments.size() < pool->max_faction_cout_limit();
  const bool balanced = pool->faction_fill_policy() == PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_FILL_BALANCED;
  placement_candidate best_placement;
  bool create_new_faction = false;
  if (!unit_can_fill_faction) {
    // 禁止补位的 Unit 只能独占一个新 faction。
    create_new_faction = true;
  } else if (balanced && can_create_faction) {
    // 平均策略优先增加 faction 数，此时不搜索已有 faction。
    create_new_faction = true;
  } else {
    // 集中策略优先补位；平均策略不能新建时，也从已有 faction 中选择。
    best_placement = find_best_existing_faction(assignments, unit_size, fill_capacity, balanced);
    create_new_faction = !best_placement.is_valid();
  }
  if (create_new_faction && !can_create_faction) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }

  auto joined_assignments = copy_assignments(assignments);
  faction_join_progress progress;
  if (create_new_faction) {
    auto* new_assignment = joined_assignments.Add();
    new_assignment->set_user_capacity(static_cast<uint32_t>(unit_can_fill_faction ? fill_capacity : unit_size));
    new_assignment->add_unit_ids(unit.unit_id());
    new_assignment->set_assigned_user_count(static_cast<uint32_t>(unit_size));
    progress.remaining_user_count = static_cast<size_t>(new_assignment->user_capacity()) - unit_size;
  } else {
    auto* joined_assignment = joined_assignments.Mutable(best_placement.position);
    joined_assignment->add_unit_ids(unit.unit_id());
    joined_assignment->set_assigned_user_count(joined_assignment->assigned_user_count() +
                                               static_cast<uint32_t>(unit_size));
    progress.joins_existing = true;
    progress.remaining_user_count = best_placement.remaining_after_join;
    progress.faction_user_count_spread_after_join = best_placement.user_count_spread_after_join;
  }

  progress.has_faction = true;
  progress.completes_faction = progress.remaining_user_count == 0;
  progress.faction_count_after_join = static_cast<size_t>(joined_assignments.size());
  if (create_new_faction) {
    progress.faction_user_count_spread_after_join = get_faction_user_count_spread(joined_assignments);
  }
  return make_join_evaluation(std::move(joined_assignments), progress);
}

matching_logic::join_check_result matching_logic::check_faction_can_join(const matching_room& room,
                                                                         const unit_view& faction_units,
                                                                         uint32_t faction_capacity, int64_t now,
                                                                         int32_t global_matching_users_count) {
  size_t faction_users = 0;
  for (const auto* unit : faction_units) {
    if (unit == nullptr) {
      return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    }
    faction_users += static_cast<size_t>(unit->users_size());
  }
  if (faction_units.empty() || faction_users != faction_capacity) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
  }
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
  }

  if (pool->faction_user_max_size() <= 0 || faction_capacity > static_cast<uint32_t>(pool->faction_user_max_size()) ||
      pool->max_faction_cout_limit() <= 0 || room.get_faction_assignments().size() >= pool->max_faction_cout_limit()) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL);
  }
  if (pool->max_user_cout_limit() > 0 &&
      room.get_user_count() + faction_users > static_cast<size_t>(pool->max_user_cout_limit())) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL);
  }

  unit_view existing_units;
  existing_units.reserve(room.get_units().size());
  for (const auto& stored : room.get_units()) {
    if (!stored.second) {
      return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
    }
    existing_units.emplace_back(&stored.second->get_data());
  }
  if (!incoming_levels_are_compatible(room.get_compatible_level_ids(), faction_units)) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }

  const auto rules = select_rules(room.get_scope(), room.get_created_time(), now, global_matching_users_count);
  if (!has_compatible_rule(rules, room.get_scope(), existing_units, faction_units, faction_capacity,
                           get_max_faction_capacity(room.get_faction_assignments()))) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  auto assignments = copy_assignments(room.get_faction_assignments());
  auto* assignment = assignments.Add();
  assignment->set_user_capacity(faction_capacity);
  assignment->set_assigned_user_count(static_cast<uint32_t>(faction_users));
  for (const auto* unit : faction_units) {
    assignment->add_unit_ids(unit->unit_id());
  }
  faction_join_progress progress;
  progress.has_faction = true;
  progress.completes_faction = true;
  progress.faction_count_after_join = static_cast<size_t>(assignments.size());
  progress.faction_user_count_spread_after_join = get_faction_user_count_spread(assignments);
  return make_join_evaluation(std::move(assignments), progress);
}

PROJECT_NAMESPACE_ID::DMatchingRoomReadyEvaluation matching_logic::check_room_ready(
    const matching_room& room, int64_t now, int32_t global_matching_users_count) {
  PROJECT_NAMESPACE_ID::DMatchingRoomReadyEvaluation result;
  result.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool || room.get_units().empty()) {
    result.set_result(pool ? PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND
                           : PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
    return result;
  }
  const auto& assignments = room.get_faction_assignments();
  const size_t total_users = room.get_user_count();
  const size_t fill_capacity = excel::get_matching_pool_faction_capacity(pool->id());
  if (fill_capacity == 0 || pool->max_faction_cout_limit() <= 0 || assignments.empty()) {
    return result;
  }
  if ((pool->max_user_cout_limit() > 0 && total_users > static_cast<size_t>(pool->max_user_cout_limit())) ||
      assignments.size() > pool->max_faction_cout_limit()) {
    result.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL);
    return result;
  }
  bool all_factions_full = true;
  size_t assigned_users = 0;
  for (const auto& assignment : assignments) {
    if (assignment.assigned_user_count() == 0 || assignment.user_capacity() > fill_capacity ||
        assignment.assigned_user_count() > assignment.user_capacity()) {
      return result;
    }
    assigned_users += assignment.assigned_user_count();
    all_factions_full = all_factions_full && assignment.assigned_user_count() == assignment.user_capacity();
  }
  if (assigned_users != total_users) {
    return result;
  }
  for (const auto& rule : select_rules(room.get_scope(), room.get_created_time(), now, global_matching_users_count)) {
    if ((rule->faction_add_rule() != PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_NONE &&
         rule->faction_add_rule() != PROJECT_NAMESPACE_ID::config::EN_MATCHING_FACTION_ADD_RULE_SAME_AS_TEAM) ||
        (rule->region_limits_size() > 0 && std::find(rule->region_limits().begin(), rule->region_limits().end(),
                                                     room.get_scope().region()) == rule->region_limits().end())) {
      continue;
    }
    // 规则有效但尚未满员时仍返回成功，允许 rebalance 继续补人。
    result.set_result(0);
    const bool enough_users =
        rule->min_user_cout_limit() <= 0 || total_users >= static_cast<size_t>(rule->min_user_cout_limit());
    const bool enough_factions =
        rule->min_faction_count_limit() <= 0 || assignments.size() >= rule->min_faction_count_limit();
    if (all_factions_full && enough_users && enough_factions) {
      result.set_ready(true);
      return result;
    }
  }
  return result;
}
