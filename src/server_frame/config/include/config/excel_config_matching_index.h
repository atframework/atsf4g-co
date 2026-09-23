// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "config/excel_type_trait_setting.h"

namespace excel {
struct config_group_t;

EXCEL_CONFIG_LOADER_API void setup_matching_config(config_group_t& group);

// 允许撮合队友的 faction 固定容量，直接来自匹配池 faction_user_max_size。无效池返回 0。
EXCEL_CONFIG_LOADER_API size_t get_matching_pool_faction_capacity(int32_t pool_id);

}  // namespace excel
