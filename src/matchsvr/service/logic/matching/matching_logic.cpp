// Copyright 2026 atframework

#include "logic/matching/matching_logic.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel/config_easy_api.h>
#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config/excel_config_matching_index.h"
#include "logic/matching/matching_room.h"
#include "logic/matching/matching_utility.h"

namespace {
using matching_rule_ptr = decltype(excel::get_ExcelMatchingRule_by_id(0));
using faction_assignment_list = google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DMatchingFactionAssignment>;

bool units_are_banned(const PROJECT_NAMESPACE_ID::DMatchingUnit& left,
                      const PROJECT_NAMESPACE_ID::DMatchingUnit& right) {
  for (const auto& left_user : left.users()) {
    if (matching_utility::contains_user(right.ban_users(), left_user.user_key())) {
      return true;
    }
    for (const auto& right_user : right.users()) {
      if (matching_utility::contains_user(right_user.lasting_ban_users(), left_user.user_key()) ||
          matching_utility::contains_user(left_user.lasting_ban_users(), right_user.user_key()) ||
          matching_utility::contains_user(right_user.last_battle_users(), left_user.user_key()) ||
          matching_utility::contains_user(left_user.last_battle_users(), right_user.user_key())) {
        return true;
      }
    }
  }
  for (const auto& right_user : right.users()) {
    if (matching_utility::contains_user(left.ban_users(), right_user.user_key())) {
      return true;
    }
  }
  return false;
}

int32_t abs_diff(int32_t left, int32_t right) {
  int64_t result = static_cast<int64_t>(left) - static_cast<int64_t>(right);
  return static_cast<int32_t>(result < 0 ? -result : result);
}

bool matches_rule_item(const PROJECT_NAMESPACE_ID::config::DMatchingRuleItem& item,
                       const PROJECT_NAMESPACE_ID::DMatchingParameter& left,
                       const PROJECT_NAMESPACE_ID::DMatchingParameter& right) {
  if (item.values_size() == 0) {
    return true;
  }
  switch (item.type()) {
    case PROJECT_NAMESPACE_ID::config::EN_MATCHING_RULE_RANK_DIFF:
      return std::all_of(item.values().begin(), item.values().end(),
                         [&](int32_t limit) { return abs_diff(left.rank_level(), right.rank_level()) <= limit; });
    default:
      return true;
  }
}

std::vector<matching_rule_ptr> select_rules(const PROJECT_NAMESPACE_ID::DMatchingScope& scope, int64_t created_time,
                                            int64_t now, int32_t global_matching_users) {
  std::vector<matching_rule_ptr> result;
  auto pool = excel::get_ExcelMatchingPool_by_id(scope.matching_pool_id());
  if (!pool) {
    return result;
  }
  for (int32_t group_id : pool->rule_group_ids()) {
    auto group = excel::get_ExcelMatchingRuleGroup_by_group_id(group_id);
    if (!group || global_matching_users < group->global_user_lower() ||
        (group->global_user_upper() > 0 && global_matching_users > group->global_user_upper())) {
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

bool incoming_units_are_compatible(const matching_logic::unit_view& existing_units,
                                   const matching_logic::unit_view& incoming_units, const matching_rule_ptr& rule) {
  for (size_t incoming_index = 0; incoming_index < incoming_units.size(); ++incoming_index) {
    const auto* incoming = incoming_units[incoming_index];
    if (incoming == nullptr) {
      return false;
    }
    for (const auto* existing : existing_units) {
      if (existing == nullptr || units_are_banned(*existing, *incoming)) {
        return false;
      }
      for (const auto& item : rule->rules()) {
        if (!matches_rule_item(item, existing->parameter(), incoming->parameter())) {
          return false;
        }
      }
    }
    for (size_t previous_index = 0; previous_index < incoming_index; ++previous_index) {
      const auto* previous = incoming_units[previous_index];
      if (previous == nullptr || units_are_banned(*previous, *incoming)) {
        return false;
      }
      for (const auto& item : rule->rules()) {
        if (!matches_rule_item(item, previous->parameter(), incoming->parameter())) {
          return false;
        }
      }
    }
  }
  return true;
}

bool check_rule_limits(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                       const matching_logic::unit_view& existing_units, const matching_logic::unit_view& incoming_units,
                       const matching_rule_ptr& rule) {
  std::unordered_map<int32_t, int32_t> force_counts;
  for (const auto* unit : existing_units) {
    if (unit == nullptr) {
      return false;
    }
    ++force_counts[unit->parameter().force_type()];
  }
  for (const auto* unit : incoming_units) {
    if (unit == nullptr) {
      return false;
    }
    ++force_counts[unit->parameter().force_type()];
  }
  for (const auto& limit : rule->force_type_limits()) {
    if (limit.count() > 0 && force_counts[limit.force_type()] > limit.count()) {
      return false;
    }
  }
  return rule->region_limits_size() == 0 || std::find(rule->region_limits().begin(), rule->region_limits().end(),
                                                      scope.region()) != rule->region_limits().end();
}

template <class TAssignmentContainer>
void copy_assignments(const TAssignmentContainer& source, faction_assignment_list& output) {
  output.Clear();
  for (const auto& assignment : source) {
    protobuf_copy_message(*output.Add(), assignment);
  }
}

struct faction_layout {
  size_t pending_user_count = 0;
  std::unordered_map<size_t, size_t> faction_count_by_capacity;
  std::set<size_t> fill_enabled_faction_capacities;
};

faction_layout get_faction_layout(const matching_room& room) {
  faction_layout result;
  result.pending_user_count = room.get_pending_faction_user_count();
  result.faction_count_by_capacity = room.get_faction_count_by_capacity();
  result.fill_enabled_faction_capacities = room.get_fill_enabled_faction_capacities();
  return result;
}

bool template_contains_layout(const faction_layout& layout,
                              const excel::matching_result_template_index_t& result_template) {
  for (const auto& entry : layout.faction_count_by_capacity) {
    auto template_count = result_template.faction_count_by_capacity.find(entry.first);
    if (template_count == result_template.faction_count_by_capacity.end() || template_count->second < entry.second) {
      return false;
    }
  }
  return std::all_of(layout.fill_enabled_faction_capacities.begin(), layout.fill_enabled_faction_capacities.end(),
                     [&](size_t capacity) { return capacity == result_template.max_faction_capacity; });
}

bool template_exactly_matches_layout(const faction_layout& layout,
                                     const excel::matching_result_template_index_t& result_template) {
  return layout.pending_user_count == 0 &&
         layout.faction_count_by_capacity == result_template.faction_count_by_capacity;
}

bool incoming_levels_are_compatible(const std::vector<int32_t>& existing_level_ids,
                                    const matching_logic::unit_view& incoming_units) {
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

using result_template_index_ptr = decltype(excel::get_matching_result_template_index(0));

struct compatible_template_options {
  bool supports_without_factions = false;
  std::vector<result_template_index_ptr> templates;
};

compatible_template_options collect_compatible_template_options(const PROJECT_NAMESPACE_ID::DMatchingScope& scope,
                                                                int64_t created_time,
                                                                const matching_logic::unit_view& existing_units,
                                                                const matching_logic::unit_view& incoming_units,
                                                                size_t total_users, int64_t now,
                                                                int32_t global_matching_users) {
  compatible_template_options result;
  for (const auto& rule : select_rules(scope, created_time, now, global_matching_users)) {
    if (!incoming_units_are_compatible(existing_units, incoming_units, rule) ||
        !check_rule_limits(scope, existing_units, incoming_units, rule)) {
      continue;
    }
    if (rule->result_template_ids_size() == 0) {
      result.supports_without_factions = true;
      continue;
    }
    for (int32_t template_id : rule->result_template_ids()) {
      auto result_template = excel::get_matching_result_template_index(template_id);
      if (result_template && total_users <= result_template->total_user_count) {
        result.templates.emplace_back(std::move(result_template));
      }
    }
  }
  return result;
}

PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation make_join_evaluation(const faction_assignment_list& assignments) {
  PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation result;
  result.set_can_join(true);
  result.set_result(0);
  copy_assignments(assignments, *result.mutable_faction_assignments());
  return result;
}

PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation make_rejected_join(int32_t result_code) {
  PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation result;
  result.set_result(result_code);
  return result;
}
}  // namespace

int32_t matching_logic::validate_unit(int32_t matching_pool_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  auto pool = excel::get_ExcelMatchingPool_by_id(matching_pool_id);
  if (!pool) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND;
  }
  if (unit.unit_id() == 0 || unit.users_size() == 0 ||
      (pool->unit_max_size() > 0 && unit.users_size() > pool->unit_max_size()) ||
      (unit.faction_fill_policy() != PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_DISABLE &&
       unit.faction_fill_policy() != PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE)) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT;
  }
  if (unit.captain_user_key().user_id() == 0 || !matching_utility::unit_has_user(unit, unit.captain_user_key())) {
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

PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation matching_logic::check_unit_can_create_room(
    const PROJECT_NAMESPACE_ID::DMatchingScope& scope, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
    int32_t global_matching_users) {
  auto pool = excel::get_ExcelMatchingPool_by_id(scope.matching_pool_id());
  if (!pool) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
  }
  const size_t unit_size = static_cast<size_t>(unit.users_size());
  if (unit_size == 0 || unit.acceptable_level_ids().empty()) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_INVALID_ARGUMENT);
  }
  if (pool->user_upper() > 0 && unit_size > static_cast<size_t>(pool->user_upper())) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL);
  }

  unit_view existing_units;
  unit_view incoming_units{&unit};
  const auto options = collect_compatible_template_options(scope, now, existing_units, incoming_units, unit_size, now,
                                                           global_matching_users);
  faction_assignment_list assignments;
  if (options.supports_without_factions) {
    return make_join_evaluation(assignments);
  }

  const bool fill_enabled = unit.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE;
  std::set<uint32_t, std::greater<uint32_t>> capacities;
  for (const auto& result_template : options.templates) {
    const size_t capacity = fill_enabled ? result_template->max_faction_capacity : unit_size;
    faction_layout prospective_layout;
    ++prospective_layout.faction_count_by_capacity[capacity];
    if (fill_enabled) {
      prospective_layout.fill_enabled_faction_capacities.emplace(capacity);
    }
    if (capacity >= unit_size && template_contains_layout(prospective_layout, *result_template)) {
      capacities.emplace(static_cast<uint32_t>(capacity));
    }
  }
  if (capacities.empty()) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  auto* assignment = assignments.Add();
  assignment->set_user_capacity(*capacities.begin());
  assignment->add_unit_ids(unit.unit_id());
  return make_join_evaluation(assignments);
}

PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation matching_logic::check_unit_can_join(
    const matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit, int64_t now,
    int32_t global_matching_users) {
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool || (pool->user_upper() > 0 && room.get_user_count() + static_cast<size_t>(unit.users_size()) >
                                              static_cast<size_t>(pool->user_upper()))) {
    return make_rejected_join(pool ? PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL
                                   : PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
  }
  unit_view existing_units;
  existing_units.reserve(room.get_units().size());
  for (const auto& stored : room.get_units()) {
    existing_units.emplace_back(&stored.second);
  }
  unit_view incoming_units{&unit};
  if (!incoming_levels_are_compatible(room.get_compatible_level_ids(), incoming_units)) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }

  faction_assignment_list assignments;
  copy_assignments(room.get_faction_assignments(), assignments);
  const auto current_layout = get_faction_layout(room);
  const size_t total_users = room.get_user_count() + static_cast<size_t>(unit.users_size());
  const auto options = collect_compatible_template_options(room.get_scope(), room.get_created_time(), existing_units,
                                                           incoming_units, total_users, now, global_matching_users);
  if (assignments.empty() && options.supports_without_factions) {
    return make_join_evaluation(assignments);
  }

  struct placement_candidate {
    int position = -1;
    bool completes_faction = false;
    size_t remaining_after_join = 0;
  };
  std::vector<placement_candidate> placement_candidates;
  if (unit.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE) {
    const size_t unit_size = static_cast<size_t>(unit.users_size());
    for (int position = 0; position < assignments.size(); ++position) {
      const auto& assignment = assignments.Get(position);
      size_t assigned_users = 0;
      for (uint64_t unit_id : assignment.unit_ids()) {
        auto unit_iter = room.get_units().find(unit_id);
        if (unit_iter == room.get_units().end()) {
          assigned_users = static_cast<size_t>(assignment.user_capacity()) + 1;
          break;
        }
        assigned_users += static_cast<size_t>(unit_iter->second.users_size());
      }
      if (assigned_users >= assignment.user_capacity() ||
          unit_size > static_cast<size_t>(assignment.user_capacity()) - assigned_users) {
        continue;
      }
      const size_t remaining_after = static_cast<size_t>(assignment.user_capacity()) - assigned_users - unit_size;
      placement_candidates.push_back(placement_candidate{position, remaining_after == 0, remaining_after});
    }
  }
  std::sort(placement_candidates.begin(), placement_candidates.end(), [](const auto& left, const auto& right) {
    if (left.completes_faction != right.completes_faction) {
      return left.completes_faction;
    }
    if (left.remaining_after_join != right.remaining_after_join) {
      return left.remaining_after_join < right.remaining_after_join;
    }
    return left.position < right.position;
  });

  const bool current_layout_supported = std::any_of(
      options.templates.begin(), options.templates.end(),
      [&](const auto& result_template) { return template_contains_layout(current_layout, *result_template); });
  if (current_layout_supported && !placement_candidates.empty()) {
    const auto& candidate = placement_candidates.front();
    faction_assignment_list joined_assignments;
    copy_assignments(assignments, joined_assignments);
    joined_assignments.Mutable(candidate.position)->add_unit_ids(unit.unit_id());
    return make_join_evaluation(joined_assignments);
  }

  const size_t unit_size = static_cast<size_t>(unit.users_size());
  const bool fill_enabled = unit.faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE;
  std::set<uint32_t, std::greater<uint32_t>> capacities;
  for (const auto& result_template : options.templates) {
    const size_t capacity = fill_enabled ? result_template->max_faction_capacity : unit_size;
    auto prospective_layout = current_layout;
    ++prospective_layout.faction_count_by_capacity[capacity];
    if (fill_enabled) {
      prospective_layout.fill_enabled_faction_capacities.emplace(capacity);
    }
    if (capacity >= unit_size && template_contains_layout(prospective_layout, *result_template)) {
      capacities.emplace(static_cast<uint32_t>(capacity));
    }
  }
  if (capacities.empty()) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  auto* assignment = assignments.Add();
  assignment->set_user_capacity(*capacities.begin());
  assignment->add_unit_ids(unit.unit_id());
  return make_join_evaluation(assignments);
}

PROJECT_NAMESPACE_ID::DMatchingUnitJoinEvaluation matching_logic::check_faction_can_join(
    const matching_room& room, const unit_view& faction_units, uint32_t faction_capacity, int64_t now,
    int32_t global_matching_users) {
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
  if (!pool ||
      (pool->user_upper() > 0 && room.get_user_count() + faction_users > static_cast<size_t>(pool->user_upper()))) {
    return make_rejected_join(pool ? PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL
                                   : PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
  }

  unit_view existing_units;
  existing_units.reserve(room.get_units().size());
  for (const auto& stored : room.get_units()) {
    existing_units.emplace_back(&stored.second);
  }
  if (!incoming_levels_are_compatible(room.get_compatible_level_ids(), faction_units)) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }

  faction_assignment_list assignments;
  copy_assignments(room.get_faction_assignments(), assignments);
  auto prospective_layout = get_faction_layout(room);
  ++prospective_layout.faction_count_by_capacity[faction_capacity];
  if (std::any_of(faction_units.begin(), faction_units.end(), [](const auto* unit) {
        return unit->faction_fill_policy() == PROJECT_NAMESPACE_ID::EN_MATCHING_FACTION_FILL_POLICY_ENABLE;
      })) {
    prospective_layout.fill_enabled_faction_capacities.emplace(faction_capacity);
  }
  const auto options =
      collect_compatible_template_options(room.get_scope(), room.get_created_time(), existing_units, faction_units,
                                          room.get_user_count() + faction_users, now, global_matching_users);
  const bool can_join = std::any_of(
      options.templates.begin(), options.templates.end(),
      [&](const auto& result_template) { return template_contains_layout(prospective_layout, *result_template); });
  if (!can_join) {
    return make_rejected_join(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  }
  auto* assignment = assignments.Add();
  assignment->set_user_capacity(faction_capacity);
  for (const auto* unit : faction_units) {
    assignment->add_unit_ids(unit->unit_id());
  }
  return make_join_evaluation(assignments);
}

PROJECT_NAMESPACE_ID::DMatchingRoomReadyEvaluation matching_logic::check_room_ready(const matching_room& room,
                                                                                    int64_t now,
                                                                                    int32_t global_matching_users) {
  unit_view units;
  units.reserve(room.get_units().size());
  for (const auto& stored : room.get_units()) {
    units.emplace_back(&stored.second);
  }
  PROJECT_NAMESPACE_ID::DMatchingRoomReadyEvaluation result;
  result.set_result(PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND);
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool || units.empty()) {
    result.set_result(pool ? PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_RULE_NOT_FOUND
                           : PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND);
    return result;
  }
  const auto layout = get_faction_layout(room);
  const size_t total_users = room.get_user_count();
  const unit_view no_incoming_units;
  for (const auto& rule : select_rules(room.get_scope(), room.get_created_time(), now, global_matching_users)) {
    if (rule->result_template_ids_size() == 0) {
      if (!room.get_faction_assignments().empty() || !incoming_units_are_compatible(no_incoming_units, units, rule) ||
          !check_rule_limits(room.get_scope(), units, no_incoming_units, rule)) {
        continue;
      }
      const int32_t ready_users =
          rule->start_battle_min_user() > 0 ? rule->start_battle_min_user() : pool->user_upper();
      result.set_result(0);
      result.set_ready(ready_users > 0 && total_users >= static_cast<size_t>(ready_users));
      return result;
    }

    const size_t required_user_count =
        static_cast<size_t>(std::max({0, rule->min_total_user(), rule->start_battle_min_user()}));
    bool rule_contains_layout = false;
    int32_t exact_template_id = 0;
    for (int32_t template_id : rule->result_template_ids()) {
      auto result_template = excel::get_matching_result_template_index(template_id);
      if (!result_template || total_users > result_template->total_user_count ||
          !template_contains_layout(layout, *result_template)) {
        continue;
      }
      rule_contains_layout = true;
      if (total_users >= required_user_count && template_exactly_matches_layout(layout, *result_template)) {
        exact_template_id = template_id;
        break;
      }
    }
    if (!rule_contains_layout || !incoming_units_are_compatible(no_incoming_units, units, rule) ||
        !check_rule_limits(room.get_scope(), units, no_incoming_units, rule)) {
      continue;
    }
    result.set_result(0);
    if (exact_template_id != 0) {
      result.set_ready(true);
      result.set_result_template_id(exact_template_id);
      return result;
    }
  }
  return result;
}
