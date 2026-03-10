#include "config/excel_config_rank_index.h"

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "config/excel/config_manager.h"
#include "config/excel/config_set_ExcelRankPeriodRewardPool.h"

namespace excel {

EXCEL_CONFIG_LOADER_API void setup_rank_config(config_group_t& group) {
  using rank_period_reward_pool_type = PROJECT_NAMESPACE_ID::config::config_set_ExcelRankPeriodRewardPool;
  for (auto& reward_group : group.ExcelRankPeriodRewardPool.get_all_of_rank_reward_pool_id()) {
    std::sort(
        const_cast<std::vector<rank_period_reward_pool_type::item_ptr_type>*>(reward_group.second.get())->begin(),
        const_cast<std::vector<rank_period_reward_pool_type::item_ptr_type>*>(reward_group.second.get())->end(),
        [](const rank_period_reward_pool_type::item_ptr_type& l, const rank_period_reward_pool_type::item_ptr_type& r) {
          return l->rank_or_score() < r->rank_or_score();
        });
  }
}

EXCEL_CONFIG_LOADER_API
::excel::excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelRankPeriodRewardPool>
get_rank_settle_rewards(const ::excel::excel_config_type_traits::shared_ptr<config_group_t>& group,
                        int32_t rank_reward_pool_id, int32_t rank_reward_pool_type, uint32_t rank_no,
                        int64_t rank_score) {
  if (rank_reward_pool_type != ::PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_BY_RANK &&
      rank_reward_pool_type != ::PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_BY_SCORE) {
    return nullptr;
  }

  auto cfg = group->ExcelRankPeriodRewardPool.get_list_by_rank_reward_pool_id(rank_reward_pool_id);
  if (!cfg) {
    return nullptr;
  }

  if (cfg->empty()) {
    return nullptr;
  }

  int64_t rank_or_score;
  if (rank_reward_pool_type == ::PROJECT_NAMESPACE_ID::EN_RANK_PREWARD_TYPE_BY_RANK) {
    rank_or_score = rank_no;
  } else {
    rank_or_score = rank_score;
  }

  auto reward_iter = std::upper_bound(
      cfg->begin(), cfg->end(), rank_or_score,
      [](int64_t l, const PROJECT_NAMESPACE_ID::config::config_set_ExcelRankPeriodRewardPool::item_ptr_type& r) {
        return l < r->rank_or_score();
      });

  if (reward_iter == cfg->begin()) {
    return nullptr;
  }

  if (reward_iter == cfg->end()) {
    return *cfg->rbegin();
  }

  --reward_iter;
  return *reward_iter;
}

EXCEL_CONFIG_LOADER_API
::excel::excel_config_type_traits::shared_ptr<const ::PROJECT_NAMESPACE_ID::config::ExcelRankPeriodRewardPool>
get_current_rank_settle_rewards(int32_t rank_reward_pool_id, int32_t rank_reward_pool_type, uint32_t rank_no,
                                int64_t rank_score) {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return nullptr;
  }

  return get_rank_settle_rewards(group, rank_reward_pool_id, rank_reward_pool_type, rank_no, rank_score);
}


}  // namespace excel
