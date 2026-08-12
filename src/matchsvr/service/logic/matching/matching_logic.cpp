// Copyright 2026 atframework

#include "logic/matching/matching_logic.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel/config_easy_api.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <vector>

#include "logic/matching/matching_room.h"
#include "logic/matching/matching_utility.h"

namespace {
using matching_rule_ptr = decltype(excel::get_ExcelMatchingRule_by_id(0));

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

std::vector<matching_rule_ptr> select_rules(const matching_room& room, int64_t now, int32_t global_matching_users) {
  std::vector<matching_rule_ptr> result;
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool) {
    return result;
  }

  for (int32_t group_id : pool->rule_group_ids()) {
    auto group = excel::get_ExcelMatchingRuleGroup_by_group_id(group_id);
    if (!group || global_matching_users < group->global_user_lower() ||
        (group->global_user_upper() > 0 && global_matching_users > group->global_user_upper())) {
      continue;
    }
    const int64_t elapsed = std::max<int64_t>(0, now - room.get_created_time());
    for (int32_t rule_id : group->pool_rules()) {
      auto rule = excel::get_ExcelMatchingRule_by_id(rule_id);
      if (!rule || elapsed < rule->time_limit().min() ||
          (rule->time_limit().max() > 0 && elapsed > rule->time_limit().max())) {
        continue;
      }
      result.emplace_back(rule);
    }
    // 规则组按全服匹配人数互斥选择；命中一个组后只尝试该组内当前时间可用的规则。
    return result;
  }
  return result;
}

bool is_unit_compatible_with_room(const matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& candidate,
                                  const matching_rule_ptr& rule) {
  for (const auto& stored : room.get_units()) {
    if (units_are_banned(stored.second, candidate)) {
      return false;
    }
    for (const auto& item : rule->rules()) {
      if (!matches_rule_item(item, stored.second.parameter(), candidate.parameter())) {
        return false;
      }
    }
  }
  return true;
}

bool check_rule_limits(const matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit* candidate,
                       const matching_rule_ptr& rule) {
  std::unordered_map<int32_t, int32_t> force_counts;
  int32_t challenge_teams = 0;
  int32_t thief_teams = 0;
  for (const auto& stored : room.get_units()) {
    ++force_counts[stored.second.parameter().force_type()];
    challenge_teams += stored.second.parameter().challenge_team() ? 1 : 0;
    thief_teams += stored.second.parameter().thief_team() ? 1 : 0;
  }
  if (candidate != nullptr) {
    ++force_counts[candidate->parameter().force_type()];
    challenge_teams += candidate->parameter().challenge_team() ? 1 : 0;
    thief_teams += candidate->parameter().thief_team() ? 1 : 0;
  }
  for (const auto& limit : rule->force_type_limits()) {
    if (limit.count() > 0 && force_counts[limit.force_type()] > limit.count()) {
      return false;
    }
  }
  if (rule->region_limits_size() > 0 && std::find(rule->region_limits().begin(), rule->region_limits().end(),
                                                  room.get_scope().region()) == rule->region_limits().end()) {
    return false;
  }
  return true;
}

std::map<int32_t, int32_t> get_team_composition(const matching_room& room) {
  std::map<int32_t, int32_t> actual;
  for (const auto& stored : room.get_units()) {
    ++actual[stored.second.users_size()];
  }
  return actual;
}

std::map<int32_t, int32_t> get_team_composition(
    const PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate& result_template) {
  std::map<int32_t, int32_t> expected;
  for (const auto& entry : result_template.team_template()) {
    expected[entry.user_number()] += entry.count();
  }
  return expected;
}

bool template_can_accept_unit(const matching_room& room, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                              const PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate& result_template) {
  auto actual = get_team_composition(room);
  ++actual[unit.users_size()];
  const auto expected = get_team_composition(result_template);
  for (const auto& value : actual) {
    auto iter = expected.find(value.first);
    if (iter == expected.end() || value.second > iter->second) {
      return false;
    }
  }
  return true;
}

bool template_can_contain_room(const matching_room& room,
                               const PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate& result_template) {
  const auto actual = get_team_composition(room);
  const auto expected = get_team_composition(result_template);
  for (const auto& value : actual) {
    auto iter = expected.find(value.first);
    if (iter == expected.end() || value.second > iter->second) {
      return false;
    }
  }
  return true;
}

bool template_is_complete(const matching_room& room,
                          const PROJECT_NAMESPACE_ID::config::ExcelMatchingResultTemplate& result_template) {
  return get_team_composition(room) == get_team_composition(result_template);
}
}  // namespace

int32_t matching_logic::validate_unit(int32_t matching_pool_id, const PROJECT_NAMESPACE_ID::DMatchingUnit& unit) {
  auto pool = excel::get_ExcelMatchingPool_by_id(matching_pool_id);
  if (!pool) {
    return PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND;
  }
  if (unit.unit_id() == 0 || unit.users_size() == 0 ||
      (pool->team_max_size() > 0 && unit.users_size() > pool->team_max_size())) {
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

matching_logic::unit_join_result matching_logic::check_unit_can_join(const matching_room& room,
                                                                     const PROJECT_NAMESPACE_ID::DMatchingUnit& unit,
                                                                     int64_t now, int32_t global_matching_users) {
  unit_join_result result;
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool) {
    result.result = PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND;
    return result;
  }
  if (pool->user_upper() > 0 &&
      static_cast<int32_t>(room.get_user_count()) + unit.users_size() > pool->user_upper()) {
    result.result = PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_ROOM_FULL;
    return result;
  }

  for (const auto& rule : select_rules(room, now, global_matching_users)) {
    if (!is_unit_compatible_with_room(room, unit, rule) || !check_rule_limits(room, &unit, rule)) {
      continue;
    }
    if (rule->result_template_ids_size() == 0) {
      result.can_join = true;
      result.result = 0;
      return result;
    }
    for (int32_t template_id : rule->result_template_ids()) {
      auto result_template = excel::get_ExcelMatchingResultTemplate_by_id(template_id);
      if (!result_template || !template_can_accept_unit(room, unit, *result_template)) {
        continue;
      }
      result.can_join = true;
      result.result_template_id = template_id;
      result.result = 0;
      return result;
    }
  }
  return result;
}

matching_logic::room_ready_result matching_logic::check_room_ready(const matching_room& room, int64_t now,
                                                                   int32_t global_matching_users) {
  room_ready_result result;
  auto pool = excel::get_ExcelMatchingPool_by_id(room.get_scope().matching_pool_id());
  if (!pool) {
    result.result = PROJECT_NAMESPACE_ID::EN_MATCHING_RESULT_POOL_NOT_FOUND;
    return result;
  }
  const int32_t total_users = static_cast<int32_t>(room.get_user_count());
  for (const auto& rule : select_rules(room, now, global_matching_users)) {
    if (!check_rule_limits(room, nullptr, rule)) {
      continue;
    }
    if (rule->result_template_ids_size() == 0) {
      const int32_t ready_users =
          rule->start_battle_min_user() > 0 ? rule->start_battle_min_user() : pool->user_upper();
      result.ready = ready_users > 0 && total_users >= ready_users;
      result.result = 0;
      if (result.ready) {
        return result;
      }
      continue;
    }
    for (int32_t template_id : rule->result_template_ids()) {
      auto result_template = excel::get_ExcelMatchingResultTemplate_by_id(template_id);
      if (!result_template || !template_can_contain_room(room, *result_template)) {
        continue;
      }
      result.result_template_id = template_id;
      result.result = 0;
      result.ready = total_users >= std::max(rule->min_total_user(), rule->start_battle_min_user()) &&
                     template_is_complete(room, *result_template);
      if (result.ready) {
        return result;
      }
    }
  }
  return result;
}
