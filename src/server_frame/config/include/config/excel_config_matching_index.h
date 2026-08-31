// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "config/excel_type_trait_setting.h"

namespace excel {
struct config_group_t;

struct matching_result_template_index_t {
  std::unordered_map<size_t, size_t> faction_count_by_capacity;
  int32_t max_faction_capacity = 0;
  int32_t total_user_count = 0;
};

using matching_result_template_index_map_t =
    std::unordered_map<int32_t, ::excel::excel_config_type_traits::shared_ptr<const matching_result_template_index_t>>;

EXCEL_CONFIG_LOADER_API void setup_matching_config(config_group_t& group);

EXCEL_CONFIG_LOADER_API ::excel::excel_config_type_traits::shared_ptr<const matching_result_template_index_t>
get_matching_result_template_index(int32_t template_id);

// 判断结果模板能否包含当前已有的 faction 容量分布。
EXCEL_CONFIG_LOADER_API bool matching_result_template_contains_faction_counts(
    const matching_result_template_index_t& result_template,
    const std::unordered_map<size_t, size_t>& faction_count_by_capacity) noexcept;

// 在当前布局合法的前提下，判断结果模板能否再增加一个指定容量的 faction。
EXCEL_CONFIG_LOADER_API bool matching_result_template_can_add_faction(
    const matching_result_template_index_t& result_template,
    const std::unordered_map<size_t, size_t>& faction_count_by_capacity, size_t faction_capacity) noexcept;

}  // namespace excel
