#pragma once

#include <config/server_frame_build_feature.h>

#include <dispatcher/task_action_no_req_base.h>
#include <dispatcher/task_type_traits.h>

#include <rpc/rpc_common_types.h>
#include <rpc/rpc_shared_message.h>

#include <chrono>
#include <memory>
#include <vector>

#include "config/excel_type_trait_setting.h"

PROJECT_NAMESPACE_BEGIN
namespace config {
class ExcelRankRule;
}
PROJECT_NAMESPACE_END

namespace excel {
struct config_group_t;
}

namespace tdr2pb {
namespace local {
class TABLE_RANK_SETTLEMENT_DEF;
}
}  // namespace tdr2pb

class logic_rank_handle_variant;

class task_action_rank_update_settlement : public task_action_no_req_base {
 public:
  struct ctor_param_t : public task_action_no_req_base::ctor_param_t {
    std::chrono::system_clock::time_point timeout;
  };

 public:
  using task_action_no_req_base::operator();

 public:
  explicit task_action_rank_update_settlement(ctor_param_t&& param);
  ~task_action_rank_update_settlement();

  result_type operator()() override;

  int on_complete() override;

 private:
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type await_all(rpc::context& ctx,
                                                          const std::vector<task_type_trait::task_type>& tasks);

  void check_trigger_exit(rpc::context& ctx, bool& allow_continue, task_type_trait::task_status current_status);

  // 返回是否需要重试失败或执行下一轮
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type settle_rank_once(
      rpc::context& ctx, bool& allow_continue,
      const ::excel::excel_config_type_traits::shared_ptr<excel::config_group_t>& group,
      const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg, logic_rank_handle_variant& rank_handle,
      uint32_t settle_loop_count, bool has_daily_reword, time_t daily_settlement_id, bool has_custom_reword,
      time_t custom_settlement_id,
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>& rank_settlement_dbdata,
      uint64_t& rank_settlement_dbversion);

  // 返回是否需要执行下一轮
  EXPLICIT_NODISCARD_ATTR rpc::result_code_type cleanup_save(
      rpc::context& ctx, bool& allow_continue, const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg,
      logic_rank_handle_variant& rank_handle, uint32_t settle_loop_count,
      rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>& rank_settlement_dbdata,
      uint64_t& rank_settlement_dbversion, bool& hold_optimistic_lock);

  EXPLICIT_NODISCARD_ATTR rpc::result_code_type process_rank(
      rpc::context& ctx, bool& allow_continue,
      const ::excel::excel_config_type_traits::shared_ptr<excel::config_group_t>& group,
      const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg, uint32_t settle_loop_count);

 private:
  ctor_param_t param_;
  std::chrono::system_clock::time_point start_timepoint_;
};
