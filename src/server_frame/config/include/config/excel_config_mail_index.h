// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <config/compile_optimize.h>
#include <gsl/select-gsl.h>

#include <cstdint>
#include <unordered_set>
#include <utility>

#include "config/excel_type_trait_setting.h"

namespace excel {
struct config_group_t;

EXCEL_CONFIG_LOADER_API void setup_mail_config(config_group_t& group);

namespace mail {

EXCEL_CONFIG_LOADER_API bool is_valid_user_mail(const excel_config_type_traits::shared_ptr<config_group_t>& group,
                                                int32_t major_type) noexcept;
EXCEL_CONFIG_LOADER_API bool is_valid_user_mail(int32_t major_type) noexcept;
EXCEL_CONFIG_LOADER_API
std::pair<excel_config_type_traits::shared_ptr<config_group_t>, std::reference_wrapper<std::unordered_set<int32_t>>>
get_all_user_mail_major_types() noexcept;

EXCEL_CONFIG_LOADER_API bool is_valid_global_mail(const excel_config_type_traits::shared_ptr<config_group_t>& group,
                                                  int32_t major_type) noexcept;
EXCEL_CONFIG_LOADER_API bool is_valid_global_mail(int32_t major_type) noexcept;
EXCEL_CONFIG_LOADER_API
std::pair<excel_config_type_traits::shared_ptr<config_group_t>, std::reference_wrapper<std::unordered_set<int32_t>>>
get_all_global_mail_major_types() noexcept;

}  // namespace mail
}  // namespace excel
