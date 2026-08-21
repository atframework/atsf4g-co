// Copyright 2026 atframework

#include "config/excel_config_matching_index.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.matching.config.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <tuple>
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

      const size_t faction_capacity = static_cast<size_t>(entry.user_number());
      index.faction_count_by_capacity[faction_capacity] += static_cast<size_t>(entry.count());
      if (faction_capacity > index.max_faction_capacity) {
        index.max_faction_capacity = faction_capacity;
      }
      for (int32_t faction_index = 0; faction_index < entry.count(); ++faction_index) {
        index.faction_capacities.emplace_back(faction_capacity);
      }
      index.total_user_count += faction_capacity * static_cast<size_t>(entry.count());
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

}  // namespace excel
