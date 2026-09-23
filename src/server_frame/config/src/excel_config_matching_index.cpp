// Copyright 2026 atframework

#include "config/excel_config_matching_index.h"

#include "config/excel/config_manager.h"

namespace excel {

EXCEL_CONFIG_LOADER_API void setup_matching_config(config_group_t& group) {
  group.matching_pool_faction_capacity.clear();
  for (const auto& entry : group.ExcelMatchingPool.get_all_of_id()) {
    if (!entry.second) {
      continue;
    }
    const auto& pool = *entry.second;
    const size_t capacity = pool.unit_max_size() > 0 && pool.faction_user_max_size() > 0
                                ? static_cast<size_t>(pool.faction_user_max_size())
                                : 0;
    group.matching_pool_faction_capacity.emplace(pool.id(), capacity);
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

}  // namespace excel
