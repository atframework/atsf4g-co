// Copyright 2026 atframework

#include "config/excel_config_matching_index.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <tuple>
#include <unordered_map>
#include <utility>

#include "config/excel/config_manager.h"

namespace excel {

EXCEL_CONFIG_LOADER_API void setup_matching_config(config_group_t& group) {
  group.matching_result_template_index.clear();
  for (const auto& result_template : group.ExcelMatchingResultTemplate.get_all_of_id()) {
    if (!result_template.second) {
      continue;
    }

    matching_result_template_index_t index;
    for (const auto& entry : result_template.second->faction_template()) {
      if (entry.user_number() <= 0 || entry.count() <= 0) {
        continue;
      }

      const int32_t user_number = entry.user_number();
      index.faction_count_by_capacity[user_number] += entry.count();
      if (user_number > index.max_faction_capacity) {
        index.max_faction_capacity = user_number;
      }
      index.total_user_count += user_number * entry.count();
    }
    group.matching_result_template_index.emplace(
        std::get<0>(result_template.first),
        ::excel::excel_config_type_traits::make_shared<const matching_result_template_index_t>(std::move(index)));
  }
}

EXCEL_CONFIG_LOADER_API ::excel::excel_config_type_traits::shared_ptr<const matching_result_template_index_t>
get_matching_result_template_index(int32_t template_id) {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return nullptr;
  }

  auto iter = group->matching_result_template_index.find(template_id);
  if (iter == group->matching_result_template_index.end()) {
    return nullptr;
  }
  return iter->second;
}

EXCEL_CONFIG_LOADER_API bool matching_result_template_contains_faction_counts(
    const matching_result_template_index_t& result_template,
    const std::unordered_map<size_t, size_t>& faction_count_by_capacity) noexcept {
  for (const auto& [capacity, count] : faction_count_by_capacity) {
    auto template_iter = result_template.faction_count_by_capacity.find(capacity);
    if (template_iter == result_template.faction_count_by_capacity.end() || template_iter->second < count) {
      return false;
    }
  }
  return true;
}

EXCEL_CONFIG_LOADER_API bool matching_result_template_can_add_faction(
    const matching_result_template_index_t& result_template,
    const std::unordered_map<size_t, size_t>& faction_count_by_capacity, size_t faction_capacity) noexcept {
  auto template_iter = result_template.faction_count_by_capacity.find(faction_capacity);
  if (template_iter == result_template.faction_count_by_capacity.end()) {
    return false;
  }
  auto current_iter = faction_count_by_capacity.find(faction_capacity);
  return current_iter == faction_count_by_capacity.end() || current_iter->second < template_iter->second;
}

}  // namespace excel
