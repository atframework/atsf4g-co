#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include <ctime>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config/excel_type_trait_setting.h"

PROJECT_NAMESPACE_BEGIN
namespace config {
class ExcelRankPeriodRewardPool;
class ExcelSeasonRule;
class ExcelRankConfig;
}  // namespace config
PROJECT_NAMESPACE_END

namespace excel {
struct config_group_t;

struct rank_report_data_id_t {
  int32_t data_id;
  uint32_t rank_type;
  uint32_t instance_id;
};

struct rank_key_t {
  uint32_t rank_type;
  uint32_t instance_id;
};

struct rank_report_role_type_data_t {
  int32_t role_type;
  std::vector<rank_report_data_id_t> datas;
};

struct rank_index_t {
  using rank_report_map_t = std::unordered_map<int32_t, std::vector<rank_report_role_type_data_t>>;
  using instance_rank_set_t = std::unordered_map<int32_t, std::vector<rank_key_t>>;
  std::unordered_map<int32_t, rank_report_map_t> rank_report_group_index;
  std::unordered_map<int32_t, instance_rank_set_t> instance_rank_group_index;
};

EXCEL_CONFIG_LOADER_API void setup_rank_config(config_group_t& group);

EXCEL_CONFIG_LOADER_API
::excel::excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelRankPeriodRewardPool>
get_rank_settle_rewards(const ::excel::excel_config_type_traits::shared_ptr<config_group_t>& group,
                        int32_t rank_reward_pool_id, int32_t rank_reward_pool_type, uint32_t rank_no,
                        int64_t rank_score);

EXCEL_CONFIG_LOADER_API
::excel::excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelRankPeriodRewardPool>
get_current_rank_settle_rewards(int32_t rank_reward_pool_id, int32_t rank_reward_pool_type, uint32_t rank_no,
                                int64_t rank_score);

EXCEL_CONFIG_LOADER_API ::excel::excel_config_type_traits::shared_ptr<std::vector<int32_t>>
get_rank_ban_rules_by_ban_id(int32_t ban_id);

EXCEL_CONFIG_LOADER_API ::excel::excel_config_type_traits::shared_ptr<std::vector<int32_t>>
get_rank_ban_ids_by_ban_rule(int32_t ban_rule_id);

EXCEL_CONFIG_LOADER_API
::excel::excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelSeasonRule>
get_current_or_next_rank_season(int32_t group_id, time_t now);

EXCEL_CONFIG_LOADER_API
::excel::excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelSeasonRule>
get_last_rank_season(int32_t group_id, time_t now);

}  // namespace excel
