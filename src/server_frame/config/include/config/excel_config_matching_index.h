// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "config/excel_type_trait_setting.h"

namespace excel {
struct config_group_t;

struct matching_result_template_index_t {
  std::vector<size_t> unit_size_counts;
  size_t total_user_count = 0;
};

using matching_result_template_index_map_t =
    std::unordered_map<int32_t, ::excel::excel_config_type_traits::shared_ptr<const matching_result_template_index_t>>;

EXCEL_CONFIG_LOADER_API void setup_matching_config(config_group_t& group);

EXCEL_CONFIG_LOADER_API ::excel::excel_config_type_traits::shared_ptr<const matching_result_template_index_t>
get_matching_result_template_index(int32_t template_id);
}  // namespace excel
