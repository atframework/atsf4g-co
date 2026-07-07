// Copyright 2026 atframework

#pragma once

#include <dispatcher/task_action_no_req_base.h>
#include <dispatcher/task_manager.h>

// #include <config/excel/config_manager.h>

#include <rpc/rpc_common_types.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
#include <protocol/common/com.struct.rank.common.pb.h>
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <chrono>
#include <string>
#include <vector>

class logic_rank_handle_variant;
struct logic_rank_handle_data;

PROJECT_NAMESPACE_BEGIN
namespace config {
class ExcelRankRule;
}
PROJECT_NAMESPACE_END

class task_action_rank_send_settlement : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    // excel::config_manager::config_group_ptr_t group;
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule* rank_rule_cfg = nullptr;  // lifetime equal to group
    uint64_t user_id = 0;
    uint32_t zone_id = 0;
    int32_t instance_type = 0;
    int64_t instance_id = 0;
    uint32_t score = 0;
    uint32_t rank_no = 0;
    uint32_t settle_rank_no = 0;
    std::vector<uint32_t> sort_fields;
    std::vector<uint64_t> ext_fields;
    uint32_t reward_rank_type = 0;
    uint32_t current_rank_type = 0;
    uint32_t copyto_rank_type = 0;
    int32_t daily_settlement_pool_id = 0;
    PROJECT_NAMESPACE_ID::EnRankPeriodRewardType daily_settlement_pool_type =
        PROJECT_NAMESPACE_ID::EnRankPeriodRewardType{};
    int64_t daily_settlement_day_id = 0;
    int32_t custom_settlement_pool_id = 0;
    PROJECT_NAMESPACE_ID::EnRankPeriodRewardType custom_settlement_pool_type =
        PROJECT_NAMESPACE_ID::EnRankPeriodRewardType{};
    int64_t custom_settlement_season_id = 0;
    int64_t mirror_id = 0;
    bool save_history = false;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_rank_send_settlement(ctor_param_t&& param);
  ~task_action_rank_send_settlement() override;

  result_type operator()() override;

 private:
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type settle_daily_rewards(rpc::context& ctx,
                                                                          logic_rank_handle_variant& rank_handle,
                                                                          const std::string& user_openid,
                                                                          int64_t& sub_score, int64_t& reset_score,
                                                                          int32_t& score_cahgne_type);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type settle_custom_rewards(rpc::context& ctx,
                                                                           logic_rank_handle_variant& rank_handle,
                                                                           const std::string& user_openid,
                                                                           int64_t& sub_score, int64_t& reset_score,
                                                                           int32_t& score_cahgne_type);

  ATFW_EXPLICIT_NODISCARD_ATTR static rpc::result_code_type settle_special_rule(
      rpc::context& ctx, logic_rank_handle_variant& rank_handle, const std::string& user_openid, int64_t& sub_score,
      int64_t& reset_score, int32_t& score_cahgne_type);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type update_sub_period_score(rpc::context& ctx,
                                                                             logic_rank_handle_variant& rank_handle,
                                                                             const std::string& user_openid,
                                                                             int64_t sub_score, int64_t reset_score);

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type save_history(rpc::context& ctx,
                                                                  logic_rank_handle_variant& rank_handle);

 private:
  ctor_param_t param_;
};
