// Copyright 2026 atframework

#include "config/excel_config_matching_index.h"

#include <log/log_wrapper.h>

#include "config/excel/config_manager.h"

namespace excel {
namespace {

static bool is_waiting_room_rule_valid(const ::PROJECT_NAMESPACE_ID::config::ExcelMatchingWaitingRoomRule& rule) {
  if (rule.id() <= 0) {
    FWLOGERROR("[EXCEL] invalid waiting room rule, id={}", rule.id());
    return false;
  }
  return true;
}

static excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelMatchingWaitingRoomRule>
find_waiting_room_rule(config_group_t& group, const ::PROJECT_NAMESPACE_ID::config::ExcelMatchingPool& pool) {
  if (!pool.is_has_waiting_room()) {
    return nullptr;
  }
  if (pool.waiting_room_rule_id() <= 0) {
    FWLOGERROR("[EXCEL] enabled waiting room has invalid rule reference, pool_id={}, rule_id={}", pool.id(),
               pool.waiting_room_rule_id());
    return nullptr;
  }
  auto rule = group.ExcelMatchingWaitingRoomRule.get_by_id(pool.waiting_room_rule_id());
  if (!rule) {
    FWLOGERROR("[EXCEL] enabled waiting room rule not found, pool_id={}, rule_id={}", pool.id(),
               pool.waiting_room_rule_id());
    return nullptr;
  }
  if (!is_waiting_room_rule_valid(*rule)) {
    FWLOGERROR("[EXCEL] enabled waiting room has invalid rule, pool_id={}, rule_id={}", pool.id(),
               pool.waiting_room_rule_id());
    return nullptr;
  }
  return rule;
}

}  // namespace

EXCEL_CONFIG_LOADER_API void setup_matching_config(config_group_t& group) {
  group.matching_pool_faction_capacity.clear();
  for (const auto& entry : group.ExcelMatchingWaitingRoomRule.get_all_of_id()) {
    if (entry.second) {
      is_waiting_room_rule_valid(*entry.second);
    }
  }
  for (const auto& entry : group.ExcelMatchingPool.get_all_of_id()) {
    if (!entry.second) {
      continue;
    }
    const auto& pool = *entry.second;
    const size_t capacity = pool.unit_max_size() > 0 && pool.faction_user_max_size() > 0
                                ? static_cast<size_t>(pool.faction_user_max_size())
                                : 0;
    group.matching_pool_faction_capacity.emplace(pool.id(), capacity);
    if (pool.is_has_waiting_room()) {
      find_waiting_room_rule(group, pool);
    }
  }
}

EXCEL_CONFIG_LOADER_API size_t get_matching_pool_faction_capacity(int32_t pool_id) {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return 0;
  }
  auto found = group->matching_pool_faction_capacity.find(pool_id);
  return found == group->matching_pool_faction_capacity.end() ? 0 : found->second;
}

EXCEL_CONFIG_LOADER_API
excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelMatchingWaitingRoomRule>
get_matching_waiting_room_rule(int32_t pool_id) {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return nullptr;
  }
  auto pool = group->ExcelMatchingPool.get_by_id(pool_id);
  if (!pool) {
    return nullptr;
  }
  return find_waiting_room_rule(*group, *pool);
}

}  // namespace excel
