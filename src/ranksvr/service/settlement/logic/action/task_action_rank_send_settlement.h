#pragma once

#include <std/smart_ptr.h>

#include <dispatcher/task_action_no_req_base.h>
#include <dispatcher/task_manager.h>

#include <config/excel/config_manager.h>

#include <rpc/rpc_common_types.h>

#include <chrono>
#include <string>
#include <vector>

class logic_rank_handle_variant;
struct logic_rank_handle_data;

class task_action_rank_send_settlement : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    excel::config_manager::config_group_ptr_t group;
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule* rank_rule_cfg;  // lifetime equal to group
    uint64_t user_id;
    uint32_t zone_id;
    uint32_t instance_type;
    int64_t instance_id;
    uint32_t score;
    uint32_t rank_no;
    uint32_t settle_rank_no;
    std::vector<uint32_t> sort_fields;
    std::vector<uint64_t> ext_fields;
    uint32_t reward_rank_type;
    uint32_t current_rank_type;
    uint32_t copyto_rank_type;
    int32_t daily_settlement_pool_id;
    PROJECT_NAMESPACE_ID::EnRankPeriodRewardType daily_settlement_pool_type;
    int64_t daily_settlement_day_id;
    int32_t custom_settlement_pool_id;
    PROJECT_NAMESPACE_ID::EnRankPeriodRewardType custom_settlement_pool_type;
    int64_t custom_settlement_season_id;
    int64_t mirror_id;
    bool save_history;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_rank_send_settlement(ctor_param_t&& param);
  ~task_action_rank_send_settlement();

  result_type operator()() override;

 private:
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type settle_daily_rewards(rpc::context& ctx,
                                                                     logic_rank_handle_variant& rank_handle,
                                                                     const std::string& user_openid, int64_t& sub_score,
                                                                     int64_t& reset_score, int32_t& score_cahgne_type);
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type settle_custom_rewards(rpc::context& ctx,
                                                                      logic_rank_handle_variant& rank_handle,
                                                                      const std::string& user_openid,
                                                                      int64_t& sub_score, int64_t& reset_score,
                                                                      int32_t& score_cahgne_type);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type settle_special_rule(rpc::context& ctx,
                                                                    logic_rank_handle_variant& rank_handle,
                                                                    const std::string& user_openid, int64_t& sub_score,
                                                                    int64_t& reset_score, int32_t& score_cahgne_type);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type update_sub_period_score(rpc::context& ctx,
                                                                        logic_rank_handle_variant& rank_handle,
                                                                        const std::string& user_openid,
                                                                        int64_t sub_score, int64_t reset_score);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type save_history(rpc::context& ctx, logic_rank_handle_variant& rank_handle);


 private:
  ctor_param_t param_;
};
