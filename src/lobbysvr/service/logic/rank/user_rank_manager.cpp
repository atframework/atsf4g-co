#include "logic/rank/user_rank_manager.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.rank.config.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <gsl/select-gsl.h>
#include <time/time_utility.h>
#include <utility/rank_util.h>

#include <memory/object_allocator.h>

#include <config/excel/config_easy_api.h>
#include <config/excel_config_const_index.h>

#include <config/logic_config.h>

#include <config/excel/config_manager.h>
#include <config/excel_config_rank_index.h>

#include <rank_logic/logic_rank_algorithm.h>
#include <rank_logic/logic_rank_handle.h>

#include <rpc/lobbysvrclientservice/lobbysvrclientservice.h>
#include <rpc/rpc_async_invoke.h>

#include <utility/protobuf_mini_dumper.h>

#include "data/player.h"
#include "logic/async_jobs/user_async_jobs_manager.h"

user_rank_manager::rank_data_index::rank_data_index(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rule) noexcept
    : logic_rank_handle_key(rule) {}

user_rank_manager::rank_data_index::rank_data_index(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rule,
                                                    const logic_rank_handle_key &other) noexcept
    : logic_rank_handle_key(rule) {
  set_instance_id(other.get_instance_id());
  set_sub_instance_id(other.get_sub_instance_id());
}

user_rank_manager::rank_data_index::rank_data_index(const rank_data_index &other) noexcept
    : logic_rank_handle_key(other.get_rank_type(), other.get_instance_id(), other.get_sub_rank_type(),
                            other.get_sub_instance_id()) {}

user_rank_manager::rank_data_index::rank_data_index(uint32_t rank_type, uint32_t instance_id, uint32_t sub_rank_type,
                                                    uint32_t sub_instance_id) noexcept
    : logic_rank_handle_key(rank_type, instance_id, sub_rank_type, sub_instance_id) {}

user_rank_manager::rank_data_index &user_rank_manager::rank_data_index::operator=(
    const rank_data_index &other) noexcept {
  set_rank_type(other.get_rank_type());
  set_instance_id(other.get_instance_id());
  set_sub_rank_type(other.get_sub_rank_type());
  set_sub_instance_id(other.get_sub_instance_id());
  return *this;
}

user_rank_manager::user_rank_manager(player &owner)
    : owner_(&owner), is_dirty_(false), io_task_next_timepoint_(0), next_auto_update_score_timepoint_(0) {}

user_rank_manager::~user_rank_manager() {}

rpc::result_code_type user_rank_manager::create_init(ATFW_EXPLICIT_UNUSED_ATTR rpc::context & ctx) {
  RPC_RETURN_CODE(0);
}

void user_rank_manager::login_init(ATFW_EXPLICIT_UNUSED_ATTR rpc::context &ctx) {}
void user_rank_manager::force_refresh_feature_limit_second(rpc::context &ctx) {
  // 强制更新在线榜单，切换角色时要进行更新，非当前角色作为离线处理
  next_auto_update_score_timepoint_ = 0;
  refresh_feature_limit_second(ctx);
}

void user_rank_manager::refresh_feature_limit_second(rpc::context &ctx) {
  // 定期更新在线榜，并追加Offline榜到待拉取队列
  // if (!owner_->get_user_async_jobs_manager().is_completed_first_async_task()) {
  //   // 登录后需要执行完异步任务在处理
  //   return;
  // }
  time_t now = util::time::time_utility::get_now();
  if (next_auto_update_score_timepoint_ < now) {
    bool need_patch_io_task_timepoint = false;
    for (auto &rank_cfg : excel::get_ExcelRankRule_all_of_rank_type_rank_instance_id()) {
      if (!rank_cfg.second) {
        continue;
      }

      if (!rank_cfg.second->need_auto_update()) {
        continue;
      }

      if (!logic_rank_is_rank_valid_now(*rank_cfg.second, now)) {
        continue;
      }

      append_pending_update_without_action(rank_data_index{*rank_cfg.second}, *rank_cfg.second, false);

      need_patch_io_task_timepoint = true;
    }

    next_auto_update_score_timepoint_ =
        now + logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::lobbysvr_cfg>().rank_auto_update_interval().seconds();
    // 不能少于30秒
    if (next_auto_update_score_timepoint_ < now + 30) {
      next_auto_update_score_timepoint_ = now + 30;
    }

    if (need_patch_io_task_timepoint) {
      patch_next_io_task_timepoint(now);
    }
  }

  if (io_task_next_timepoint_ > 0 && now >= io_task_next_timepoint_) {
    try_start_io_task(ctx);
  }
}

void user_rank_manager::init_from_table_data(ATFW_EXPLICIT_UNUSED_ATTR rpc::context &ctx,
                                             const PROJECT_NAMESPACE_ID::table_user &player_table) {
  if (!player_table.has_rank_data()) {
    return;
  }

  is_dirty_ = false;
  io_task_next_timepoint_ = util::time::time_utility::get_now();
  next_auto_update_score_timepoint_ = 0;

  pending_update_score_ranks_.clear();
  pending_update_no_action_index_.clear();
  db_data_.clear();

  for (int i = 0; i < player_table.rank_data().ranks_size(); ++i) {
    const PROJECT_NAMESPACE_ID::DRankUserBoard &data = player_table.rank_data().ranks(i);

    auto rank_rule_cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(data.rank_key().rank_type(),
                                                                                data.rank_key().rank_instance_id());
    if (!rank_rule_cfg) {
      FWPLOGERROR(*owner_, "get rank data for cfg {},{} failed, not found", data.rank_key().rank_type(),
                  data.rank_key().rank_instance_id());
      continue;
    }

    auto rank_data = mutable_rank_data(rank_data_index(*rank_rule_cfg), *rank_rule_cfg, logic_rank_user_extend_span{});
    if (nullptr != rank_data) {
      protobuf_copy_message(rank_data->rank_data, data);
      for (auto &rank : *rank_data->rank_data.mutable_rank_instance_data()) {
        rank.set_mode(PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_OFFLINE);
      }
    } else {
      FWPLOGERROR(*owner_, "get rank data for cfg {},{} failed, malloc failed", data.rank_key().rank_type(),
                  data.rank_key().rank_instance_id());
    }
  }
}

int user_rank_manager::dump(ATFW_EXPLICIT_UNUSED_ATTR rpc::context &ctx, PROJECT_NAMESPACE_ID::table_user &user) {
  time_t now = util::time::time_utility::get_now();
  PROJECT_NAMESPACE_ID::DRankUserData *rank_data = user.mutable_rank_data();
  if (NULL == rank_data) {
    // FWPLOGERROR(*owner_, "player {}({}) malloc player_rank failed");
    return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
  }

  for (auto &rank : db_data_) {
    if (!rank.second) {
      continue;
    }

    protobuf_remove_repeated_if(*rank.second->rank_data.mutable_unsubmit_action(),
                                [now](const PROJECT_NAMESPACE_ID::DRankUnsubmitData &unsubmit) {
                                  return 0 != unsubmit.expired_timepoint() && unsubmit.expired_timepoint() <= now;
                                });
    protobuf_copy_message(*rank_data->add_ranks(), rank.second->rank_data);
  }
  return 0;
}

bool user_rank_manager::is_dirty() const noexcept { return is_dirty_; }

void user_rank_manager::clear_dirty() { is_dirty_ = false; }

bool user_rank_manager::is_io_task_running() const {
  if (task_type_trait::empty(io_task_)) {
    return false;
  }

  if (task_type_trait::is_exiting(io_task_)) {
    task_type_trait::reset_task(io_task_);
    return false;
  }

  return true;
}

void user_rank_manager::try_start_io_task(rpc::context &ctx) {
  if (is_io_task_running()) {
    return;
  }

  time_t now = util::time::time_utility::get_now();
  if (io_task_next_timepoint_ > 0 && io_task_next_timepoint_ > util::time::time_utility::get_now()) {
    return;
  }
  io_task_next_timepoint_ = 0;
  time_t protected_timeout = logic_config::me()->get_server_cfg().user().async_job().interval().seconds();
  if (protected_timeout <= 0) {
    protected_timeout = 10;
  }
  patch_next_io_task_timepoint(now + protected_timeout);

  bool has_unsubmmit_data = false;
  for (auto &rank_data : db_data_) {
    if (!rank_data.second) {
      continue;
    }

    if (rank_data.second->rank_data.unsubmit_action_size() <= 0) {
      continue;
    }

    auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(rank_data.second->index.rank_type,
                                                                      rank_data.second->index.rank_instance_id);
    if (!cfg) {
      continue;
    }

    if (!is_rank_writable_now(*cfg, now)) {
      continue;
    }

    has_unsubmmit_data = true;
    break;
  }
  // 无IO任务直接退出
  if (pending_update_score_ranks_.empty() && !has_unsubmmit_data) {
    return;
  }

  auto user_ptr = owner_->shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "user_rank_manager.try_start_io_task", [user_ptr](rpc::context &child_ctx) -> rpc::result_code_type {
        user_rank_manager &self = user_ptr->get_user_rank_manager();
        int32_t ret = RPC_AWAIT_CODE_RESULT(self.run_io_task(child_ctx));
        while (ret >= 0 && !self.pending_update_score_ranks_.empty()) {
          ret = RPC_AWAIT_CODE_RESULT(self.run_io_task(child_ctx));
        }
        if (task_type_trait::get_task_id(self.io_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self.io_task_);
        }
        RPC_RETURN_CODE(ret);
      });

  if (invoke_result.is_error()) {
    FWPLOGERROR(*owner_, "async_invoke a rank io task failed.res: {}({})", *invoke_result.get_error(),
                protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  } else {
    if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
      io_task_ = std::move(*invoke_result.get_success());
    }
  }
}

rpc::result_code_type user_rank_manager::wait_for_async_task(rpc::context &ctx) {
  while (is_io_task_running()) {
    if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
      break;
    }

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (res < 0) {
      FWPLOGERROR(*owner_, "await io task error, res: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
      RPC_RETURN_CODE(res);
    }
  }

  RPC_RETURN_CODE(0);
}

bool user_rank_manager::is_rank_writable_now(const logic_rank_rule_cfg_t &cfg, time_t now) const noexcept {
  // TODO(owenout): 检查权限
  return logic_rank_is_rank_writable_now(cfg, now);
}

logic_rank_handle_variant user_rank_manager::get_rank_handle(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg) {
  if (logic_rank_is_global_rank(cfg)) {
    return logic_rank_handle_variant{logic_config::me()->get_local_world_id(),
                                     logic_rank_get_global_rank_shared_zone_id(cfg), cfg};
  }

  return logic_rank_handle_variant{logic_config::me()->get_local_world_id(), owner_->get_zone_id(), cfg};
}

logic_rank_handle_key user_rank_manager::get_current_rank_data_key(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg) {
  return logic_rank_handle_key{cfg};
}

logic_rank_handle_key user_rank_manager::get_current_rank_data_key(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg, uint32_t subrank_instance_id) {
  logic_rank_handle_key ret = get_current_rank_data_key(cfg);
  ret.set_sub_instance_id(subrank_instance_id);

  return ret;
}

rpc::rpc_result<user_rank_manager::rank_board_cache> user_rank_manager::get_rank_cache(
    rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key) {
  // 如果正在执行积分更新，尝试等它完成。要保证缓存一致性
  while (is_io_task_running()) {
    if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
      break;
    }

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (res < 0) {
      FWPLOGERROR(*owner_, "wait io task when get rank data of {},{},{},{} failed, res: {}({})", cfg.rank_type(),
                  cfg.rank_instance_id(), cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), res,
                  protobuf_mini_dumper_get_error_msg(res));
      RPC_RETURN_TYPE(rank_board_cache(0, 0));
    }
  }

  auto res = get_rank_data(rank_data_index(cfg, rank_key));
  if (!res) {
    RPC_RETURN_TYPE(rank_board_cache{0, 0});
  }

  time_t now = util::time::time_utility::get_now();
  check_and_settlement_local_rank_data(cfg, res, rank_instance_key, now);
  auto instance_rank = get_instance_rank_data(res, rank_instance_key);
  if (instance_rank == nullptr) {
    RPC_RETURN_TYPE(rank_board_cache{0, 0});
  }
  RPC_RETURN_TYPE(rank_board_cache{instance_rank->last_rank_no_cache(), instance_rank->last_score_cache()});
}

rpc::result_code_type user_rank_manager::set_rank_score(rpc::context &ctx, const logic_rank_handle_key &rank_key,
                                                        const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                        const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
                                                        uint32_t score, const logic_rank_user_extend_span *user_extend,
                                                        bool sync_mode) {
  if (!check_rank_instance_key_invalid(cfg, rank_instance_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_INSTANCE_KEY_INVALID);
  }

  logic_rank_handle_variant rank_handle = get_rank_handle(cfg);
  if (!rank_handle.is_service_available()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_SERVERTIME_UNAVAILABLE);
  }
  while (is_io_task_running()) {
    if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
      break;
    }

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  time_t now = util::time::time_utility::get_now();

  PROJECT_NAMESPACE_ID::DRankUnsubmitData *unsubmit =
      mutable_unsubmit_data(rank_data_index(cfg, rank_key), cfg, owner_->get_zone_id(), owner_->get_user_id(),
                            rank_instance_key, nullptr == user_extend ? logic_rank_user_extend_span() : *user_extend);
  if (nullptr == unsubmit) {
    FWPLOGERROR(*owner_, "malloc DRankUnsubmitData failed. rank key: {},{}, rank_instance_key: {}:{}",
                rank_key.get_rank_type(), rank_key.get_instance_id(), rank_instance_key.instance_type(),
                rank_instance_key.instance_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }

  unsubmit->set_is_offset(false);
  unsubmit->set_value(static_cast<int64_t>(score));
  unsubmit->set_expired_timepoint(logic_rank_get_record_expire_time(cfg, now));
  protobuf_copy_message(*unsubmit->mutable_rank_instance_key(), rank_instance_key);

  if (is_rank_writable_now(cfg, now)) {
    patch_next_io_task_timepoint(now);
    try_start_io_task(ctx);
  }

  if (sync_mode) {
    while (is_io_task_running()) {
      if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
        break;
      }

      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
      if (res < 0) {
        RPC_RETURN_CODE(res);
      }
    }
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_rank_manager::add_rank_score(rpc::context &ctx, const logic_rank_handle_key &rank_key,
                                                        const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                        uint32_t target_user_zone_id, uint64_t target_user_id,
                                                        const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
                                                        uint32_t score, logic_rank_user_extend_span user_extend,
                                                        bool sync_mode) {
  if (!check_rank_instance_key_invalid(cfg, rank_instance_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_INSTANCE_KEY_INVALID);
  }

  logic_rank_handle_variant rank_handle = get_rank_handle(cfg);
  if (!rank_handle.is_service_available()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_SERVERTIME_UNAVAILABLE);
  }
  while (is_io_task_running()) {
    if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
      break;
    }

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  time_t now = util::time::time_utility::get_now();

  PROJECT_NAMESPACE_ID::DRankUnsubmitData *unsubmit = mutable_unsubmit_data(
      rank_data_index(cfg, rank_key), cfg, target_user_zone_id, target_user_id, rank_instance_key, user_extend);

  if (nullptr == unsubmit) {
    FWPLOGERROR(*owner_, "malloc DRankUnsubmitData failed. rank key: {}:{}, rank_instance_key: {}:{}",
                rank_key.get_rank_type(), rank_key.get_instance_id(), rank_instance_key.instance_type(),
                rank_instance_key.instance_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }

  // 不能设置 is_offset ，如果先有 set_rank_score，再 add_rank_score/sub_rank_score , 也要保留非offset模式
  unsubmit->set_value(unsubmit->value() + static_cast<int64_t>(score));
  unsubmit->set_expired_timepoint(logic_rank_get_record_expire_time(cfg, now));
  protobuf_copy_message(*unsubmit->mutable_rank_instance_key(), rank_instance_key);

  if (is_rank_writable_now(cfg, now)) {
    patch_next_io_task_timepoint(now);
    try_start_io_task(ctx);
  }

  if (sync_mode) {
    while (is_io_task_running()) {
      if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
        break;
      }

      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
      if (res < 0) {
        RPC_RETURN_CODE(res);
      }
    }
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_rank_manager::sub_rank_score(rpc::context &ctx, const logic_rank_handle_key &rank_key,
                                                        const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                        uint32_t target_user_zone_id, uint64_t target_user_id,
                                                        const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
                                                        uint32_t score, logic_rank_user_extend_span user_extend,
                                                        bool sync_mode) {
  if (!check_rank_instance_key_invalid(cfg, rank_instance_key)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_INSTANCE_KEY_INVALID);
  }

  logic_rank_handle_variant rank_handle = get_rank_handle(cfg);
  if (!rank_handle.is_service_available()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_SERVERTIME_UNAVAILABLE);
  }
  while (is_io_task_running()) {
    if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
      break;
    }

    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (res < 0) {
      RPC_RETURN_CODE(res);
    }
  }

  time_t now = util::time::time_utility::get_now();

  PROJECT_NAMESPACE_ID::DRankUnsubmitData *unsubmit = mutable_unsubmit_data(
      rank_data_index(cfg, rank_key), cfg, target_user_zone_id, target_user_id, rank_instance_key, user_extend);
  if (nullptr == unsubmit) {
    FWPLOGERROR(*owner_, "malloc DRankUnsubmitData failed. rank key: {}:{}, rank_instance_key: {}:{}",
                rank_key.get_rank_type(), rank_key.get_instance_id(), rank_instance_key.instance_type(),
                rank_instance_key.instance_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }

  // 不能设置 is_offset ，如果先有 set_rank_score，再 add_rank_score/sub_rank_score , 也要保留非offset模式
  unsubmit->set_value(unsubmit->value() - static_cast<int64_t>(score));
  unsubmit->set_expired_timepoint(logic_rank_get_record_expire_time(cfg, now));
  protobuf_copy_message(*unsubmit->mutable_rank_instance_key(), rank_instance_key);

  if (is_rank_writable_now(cfg, now)) {
    patch_next_io_task_timepoint(now);
    try_start_io_task(ctx);
  }

  if (sync_mode) {
    while (is_io_task_running()) {
      if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
        break;
      }

      int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
      if (res < 0) {
        RPC_RETURN_CODE(res);
      }
    }
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_rank_manager::get_top_rank(
    rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
    uint32_t from_rank_no, uint32_t rank_count, bool ignore_zero) {
  return get_top_rank(ctx, rank_key, cfg, response, total_count, from_rank_no, rank_count, ignore_zero, true);
}

rpc::result_code_type user_rank_manager::get_self_top_rank(
    rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
    uint32_t up_count, uint32_t down_count) {
  return get_special_top_rank(ctx, rank_key, cfg, owner_->get_zone_id(), owner_->get_user_id(), rank_instance_key,
                              response, total_count, up_count, down_count);
}

rpc::result_code_type user_rank_manager::get_special_top_rank(
    rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    uint32_t target_user_zone_id, uint64_t target_user_id,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response, uint32_t &total_count,
    uint32_t up_count, uint32_t down_count) {
  return get_special_top_rank(ctx, rank_key, cfg, target_user_zone_id, target_user_id, rank_instance_key, response,
                              total_count, up_count, down_count, true);
}

rpc::result_code_type user_rank_manager::get_top_rank(
    rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response,
    ATFW_EXPLICIT_UNUSED_ATTR uint32_t &total_count, uint32_t from_rank_no, uint32_t rank_count,
    ATFW_EXPLICIT_UNUSED_ATTR bool ignore_zero, bool allow_submit_local) {

  if (PROJECT_NAMESPACE_ID::EN_RANK_LOGIC_TYPE_INVALID == rank_key.get_rank_type() ||
      !PROJECT_NAMESPACE_ID::EnRankLogicType_IsValid(static_cast<int>(rank_key.get_rank_type()))) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // 如果正在执行积分更新，尝试等它完成。要保证缓存一致性
  while (is_io_task_running()) {
    if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
      break;
    }

    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  logic_rank_handle_variant rank_handle = get_rank_handle(cfg);
  if (!rank_handle.is_service_available()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_SERVERTIME_UNAVAILABLE);
  }

  auto res = RPC_AWAIT_CODE_RESULT(rank_handle.get_top_rank(ctx, rank_key, from_rank_no, rank_count));

  if (res.api_result < 0) {
    FWPLOGERROR(*owner_, "get_top_rank {},{},{},{} failed, res: {}({})", cfg.rank_type(), cfg.rank_instance_id(),
                cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), res.api_result,
                protobuf_mini_dumper_get_error_msg(res.api_result));
    RPC_RETURN_CODE(res.api_result);
  }

  time_t now = util::time::time_utility::get_now();
  bool has_self = false;
  uint32_t score_min = std::numeric_limits<uint32_t>::max();
  for (auto cur = rank_handle.get_current_cursor(); cur != nullptr;) {
    convert_to(*response.Add(), *cur);
    if (cur->zone_id == owner_->get_zone_id() && cur->user_id == owner_->get_user_id()) {
      update_rank_score_cache(ctx, rank_data_index{cfg, rank_key}, cfg, *cur, now);
      has_self = true;
    }
    if (score_min > cur->score) {
      score_min = cur->score;
    }

    if (!rank_handle.next_cursor()) {
      break;
    }
    cur = rank_handle.get_current_cursor();
  }
  // 特殊处理拉取的最低分数比自己的低，但自己不在榜上，要触发upload
  // toDO 根据排行榜类型实现不同的的defult拉取
  // if (!has_self && !response.empty() && allow_submit_local) {
  //   rank_data_index rank_index{cfg, rank_key};
  //   auto my_rank_data = get_rank_data(rank_index);
  //   if (!my_rank_data) {
  //     RPC_RETURN_CODE(res.api_result);
  //   }
  //   auto rank_role = get_instance_rank_data(my_rank_data, get_default_role_guid());
  //   if (rank_role != nullptr) {
  //     if (rank_role->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL &&
  //         rank_role->last_score_cache() > score_min && is_rank_writable_now(cfg, now)) {
  //       check_and_settlement_local_rank_data(cfg, my_rank_data, rank_role->role_guid(), now);
  //       append_pending_update_without_action(rank_index, cfg);
  //       patch_next_io_task_timepoint(now);
  //     }
  //   }
  // }

  RPC_RETURN_CODE(res.api_result);
}

rpc::result_code_type user_rank_manager::get_special_top_rank(
    rpc::context &ctx, const logic_rank_handle_key &rank_key, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    uint32_t target_user_zone_id, uint64_t target_user_id,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> &response,
    ATFW_EXPLICIT_UNUSED_ATTR uint32_t &total_count, uint32_t up_count, uint32_t down_count, bool allow_submit_local) {

  if (PROJECT_NAMESPACE_ID::EN_RANK_LOGIC_TYPE_INVALID == rank_key.get_rank_type() ||
      !PROJECT_NAMESPACE_ID::EnRankLogicType_IsValid(static_cast<int>(rank_key.get_rank_type()))) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // 如果正在执行积分更新，尝试等它完成。要保证缓存一致性
  while (is_io_task_running()) {
    if (task_type_trait::get_task_id(io_task_) == ctx.get_task_context().task_id) {
      break;
    }

    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, io_task_));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }
  }

  logic_rank_handle_variant rank_handle = get_rank_handle(cfg);
  if (!rank_handle.is_service_available()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_SERVERTIME_UNAVAILABLE);
  }

  time_t now = util::time::time_utility::get_now();

  auto rank_define_cfg =
      excel::get_ExcelRankDefine_by_rank_type_rank_instance_id(cfg.rank_type(), cfg.rank_instance_id());
  if (!rank_define_cfg) {
    FWPLOGERROR(*owner_, "get rank define cfg for {},{} failed", cfg.rank_type(), cfg.rank_instance_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_DEFINE_CFG_NOT_FOUND);
  }

  // 拉取榜单最后一名 分数比缓存分数低的话重新上报
  do {
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUserBasicData> top_rank_datas;
    auto res = RPC_AWAIT_CODE_RESULT(
        get_top_rank(ctx, rank_key, cfg, top_rank_datas, total_count, rank_define_cfg->capacity(), 1));
    // 拉失败 不进行修复处理了
    FWLOGDEBUG("rank {}:{}, get_special_top_rank get_top_rank {}", rank_key.get_rank_type(), rank_key.get_instance_id(),
               res);
    if (res != 0) {
      break;
    }
    uint32_t rank_min_score = 0;
    for (const auto &unit : top_rank_datas) {
      rank_min_score = unit.score();
    }

    auto rank_data = get_rank_data(rank_data_index(cfg, rank_key));
    if (!rank_data) {
      break;
    }

    PROJECT_NAMESPACE_ID::DRankInstanceBoard *instance_rank = get_instance_rank_data(rank_data, rank_instance_key);
    if (!instance_rank) {
      break;
    }

    if (rank_min_score < instance_rank->last_score_cache() && is_rank_writable_now(cfg, now)) {
      // 上报
      check_and_settlement_local_rank_data(cfg, rank_data, rank_instance_key, now);

      PROJECT_NAMESPACE_ID::DRankUnsubmitData unsubmit;
      unsubmit.mutable_user_key()->set_zone_id(owner_->get_zone_id());
      unsubmit.mutable_user_key()->set_user_id(owner_->get_user_id());
      protobuf_copy_message(*unsubmit.mutable_rank_instance_key(), rank_instance_key);
      unsubmit.set_is_offset(false);
      unsubmit.set_value(instance_rank->last_score_cache());
      unsubmit.set_expired_timepoint(logic_rank_get_record_expire_time(cfg, now));
      if (instance_rank->sort_fields_size() > 0) {
        protobuf_copy_message(*unsubmit.mutable_sort_fields(), instance_rank->sort_fields());
      }

      if (instance_rank->ext_fields_size() > 0) {
        protobuf_copy_message(*unsubmit.mutable_ext_fields(), instance_rank->ext_fields());
      }
      res = RPC_AWAIT_CODE_RESULT(submit(ctx, rank_data_index(cfg, rank_key), rank_key, cfg, unsubmit, now));
      if (res != 0) {
        FWPLOGERROR(*owner_, "update cache failed for rank {},{},{} , res: {}({})", cfg.rank_type(),
                    cfg.rank_instance_id(), rank_key.get_sub_instance_id(), res,
                    protobuf_mini_dumper_get_error_msg(res));
      }
    }
  } while (false);

  std::string target_rank_openid = rank_user_key_to_openid(
      target_user_zone_id, target_user_id, rank_instance_key.instance_type(), rank_instance_key.instance_id());

  logic_rank_handle_data target_rank_data;
  auto res = RPC_AWAIT_CODE_RESULT(
      rank_handle.get_special_one(ctx, target_rank_data, rank_key, target_rank_openid, up_count, down_count));

  if (res.api_result < 0) {
    FWPLOGERROR(*owner_, "get_special_one {},{},{},{} failed, res: {}({})", cfg.rank_type(), cfg.rank_instance_id(),
                cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), res.api_result,
                protobuf_mini_dumper_get_error_msg(res.api_result));
    RPC_RETURN_CODE(res.api_result);
  }

  bool has_self = false;
  bool has_target = false;
  uint32_t score_min = std::numeric_limits<uint32_t>::max();
  if (target_rank_data.zone_id == target_user_zone_id && target_rank_data.user_id == target_user_id &&
      target_rank_data.instance_type == rank_instance_key.instance_type() &&
      target_rank_data.instance_id == rank_instance_key.instance_id()) {
    has_target = true;
    convert_to(*response.Add(), target_rank_data);
    score_min = target_rank_data.score;
    if (target_rank_data.zone_id == owner_->get_zone_id() && target_rank_data.user_id == owner_->get_user_id()) {
      update_rank_score_cache(ctx, rank_data_index{cfg, rank_key}, cfg, target_rank_data, now);
      has_self = true;
    }
  }

  for (auto cur = rank_handle.get_current_cursor(); cur != nullptr;) {
    if (cur->zone_id == target_user_zone_id && cur->user_id == target_user_id) {
      if (has_target) {
        if (!rank_handle.next_cursor()) {
          break;
        }
        cur = rank_handle.get_current_cursor();
        continue;
      } else {
        has_target = true;
      }
    }

    convert_to(*response.Add(), *cur);
    if (cur->zone_id == owner_->get_zone_id() && cur->user_id == owner_->get_user_id()) {
      update_rank_score_cache(ctx, rank_data_index{cfg, rank_key}, cfg, *cur, now);
      has_self = true;
    }
    if (score_min > cur->score) {
      score_min = cur->score;
    }

    if (!rank_handle.next_cursor()) {
      break;
    }
    cur = rank_handle.get_current_cursor();
  }

  // 如果是定向拉自己的数据且没拉到，需要重置排行榜状态为kLocal
  if (!has_self && target_user_zone_id == owner_->get_zone_id() && target_user_id == owner_->get_user_id()) {
    auto rank_data = mutable_rank_data(rank_data_index(cfg, rank_key), cfg, logic_rank_user_extend_span{});
    if (rank_data) {
      PROJECT_NAMESPACE_ID::DRankInstanceBoard *instance_rank = get_instance_rank_data(rank_data, rank_instance_key);
      if (instance_rank != nullptr) {
        instance_rank->set_mode(PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL);
        instance_rank->set_last_rank_no_cache(0);
      }
      check_and_settlement_local_rank_data(cfg, rank_data, rank_instance_key, now);
      FWPLOGDEBUG(*owner_, "self rank {},{},{},{} offline", cfg.rank_type(), cfg.rank_instance_id(),
                  cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id());
    }
  }

  // 特殊处理拉取的最低分数比自己的低，但自己不在榜上，要触发upload
  if (!has_self && !response.empty() && allow_submit_local) {
    rank_data_index rank_index{cfg, rank_key};
    auto my_rank_data = get_rank_data(rank_index);
    if (!my_rank_data) {
      RPC_RETURN_CODE(res.api_result);
    }
    auto instance_rank = get_instance_rank_data(my_rank_data, rank_instance_key);
    if (instance_rank) {
      if (instance_rank->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL &&
          instance_rank->last_score_cache() > score_min && is_rank_writable_now(cfg, now)) {
        check_and_settlement_local_rank_data(cfg, my_rank_data, rank_instance_key, now);
        append_pending_update_without_action(rank_index, cfg);
        patch_next_io_task_timepoint(now);
      }
    }
  }
  RPC_RETURN_CODE(res.api_result);
}

void user_rank_manager::reset_io_task_protect() {
  time_t now = util::time::time_utility::get_now();
  time_t next_io_task_timepoint = 0;
  for (auto &rank_data : db_data_) {
    if (!rank_data.second) {
      continue;
    }

    if (rank_data.second->rank_data.unsubmit_action_size() <= 0) {
      continue;
    }

    auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(rank_data.second->index.rank_type,
                                                                      rank_data.second->index.rank_instance_id);
    if (!cfg) {
      continue;
    }

    // 有Offline则立即重启io task
    for (auto &instance_rank : *rank_data.second->rank_data.mutable_rank_instance_data()) {
      if (instance_rank.mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_OFFLINE) {
        next_io_task_timepoint = now;
        continue;
      }

      // 有未提交且锁榜的排行榜，下一次IO任务时间为解锁时间
      if (!is_rank_writable_now(*cfg, now)) {
        time_t next_valid_timepoint = logic_rank_get_rank_next_valid_time(*cfg, now);
        if (next_valid_timepoint > now) {
          if (next_io_task_timepoint == 0 || next_io_task_timepoint > next_valid_timepoint) {
            next_io_task_timepoint = next_valid_timepoint;
          }
        }
        continue;
      }
    }

    // 有待提交数据则立即重启io task
    next_io_task_timepoint = now;
    break;
  }

  if (0 != next_io_task_timepoint && next_io_task_timepoint < io_task_next_timepoint_) {
    io_task_next_timepoint_ = next_io_task_timepoint;
  }
}

rpc::result_code_type user_rank_manager::add_settle_reward(rpc::context &ctx,
                                                           const PROJECT_NAMESPACE_ID::DRankBoardBasicData &rank_basic,
                                                           int32_t pool_id,
                                                           PROJECT_NAMESPACE_ID::EnRankPeriodRewardType pool_type,
                                                           ATFW_EXPLICIT_UNUSED_ATTR bool save_history, time_t cycle_no,
                                                           bool is_custom, time_t deliver_time, int32_t season_id) {
  auto rule_cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(rank_basic.rank_key().rank_type(),
                                                                         rank_basic.rank_key().rank_instance_id());
  if (!rule_cfg) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_INVALID_TYPE);
  }

  // 结算和清理 策划没有确定方案 先按照角色结算处理
  rank_data_index rank_index{*rule_cfg};
  auto rank_data = mutable_rank_data(rank_index, *rule_cfg, logic_rank_user_extend_span{});
  if (!rank_data) {
    FWPLOGERROR(*owner_, "add_settle_reward(pool_id={}, rank={}, score={}) with invalid rank key {},{},{},{}", pool_id,
                rank_basic.rank_no(), rank_basic.score(), rank_basic.rank_key().rank_type(),
                rank_basic.rank_key().rank_instance_id(), rank_basic.rank_key().sub_rank_type(),
                rank_basic.rank_key().sub_rank_instance_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  PROJECT_NAMESPACE_ID::DRankInstanceBoard *instance_rank =
      mutable_instance_rank_data(rank_data, rank_basic.rank_instance_key());
  if (!instance_rank) {
    FWPLOGERROR(
        *owner_,
        "add_settle_reward(pool_id={}, rank={}, score={}) with invalid rank key {},{},{},{}, rank_instance {}:{}",
        pool_id, rank_basic.rank_no(), rank_basic.score(), rank_basic.rank_key().rank_type(),
        rank_basic.rank_key().rank_instance_id(), rank_basic.rank_key().sub_rank_type(),
        rank_basic.rank_key().sub_rank_instance_id(), rank_basic.rank_instance_key().instance_type(),
        rank_basic.rank_instance_key().instance_id());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  if (instance_rank->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KONLINE) {
    append_pending_update_without_action(rank_index, *rule_cfg);
    patch_next_io_task_timepoint(util::time::time_utility::get_now());
  }

  // if (is_custom && instance_rank->custom_reward_cycle_no() >= cycle_no) {
  //   RPC_RETURN_CODE(0);
  // }

  // if (!is_custom && instance_rank->daily_reward_cycle_no() >= cycle_no) {
  //   RPC_RETURN_CODE(0);
  // }

  auto reward_cfg =
      excel::get_current_rank_settle_rewards(pool_id, pool_type, rank_basic.settle_rank_no(), rank_basic.score());
  if (!reward_cfg) {
    FWPLOGWARNING(*owner_, "try to reward rank({},{},{},{}) pool_id={}, rank={}, score={}, but no rewards found.",
                  rank_basic.rank_key().rank_type(), rank_basic.rank_key().rank_instance_id(),
                  rank_basic.rank_key().sub_rank_type(), rank_basic.rank_key().sub_rank_instance_id(), pool_id,
                  rank_basic.settle_rank_no(), rank_basic.score());
    RPC_RETURN_CODE(0);
  }

  if (is_custom) {
    instance_rank->set_custom_reward_cycle_no(cycle_no);
  } else {
    instance_rank->set_daily_reward_cycle_no(cycle_no);
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type user_rank_manager::run_io_task(rpc::context &ctx) {
  std::unordered_set<rank_data_index, logic_rank_handle_key_hash> pending_upload_index_keys;
  while (!pending_update_score_ranks_.empty()) {
    std::list<PROJECT_NAMESPACE_ID::user_async_job_update_rank> pending_update_score_ranks;
    pending_update_score_ranks_.swap(pending_update_score_ranks);
    pending_update_no_action_index_.clear();

    // uint32_t total_count = 0;
    std::unordered_set<rank_data_index, logic_rank_handle_key_hash> pulled_keys;

    for (auto &update_rank_info : pending_update_score_ranks) {
      auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(update_rank_info.rank_key().rank_type(),
                                                                        update_rank_info.rank_key().rank_instance_id());
      if (!cfg) {
        FWPLOGERROR(*owner_, "rank {},{},{},{} configure not found", update_rank_info.rank_key().rank_type(),
                    update_rank_info.rank_key().rank_instance_id(), update_rank_info.rank_key().sub_rank_type(),
                    update_rank_info.rank_key().sub_rank_instance_id());
        continue;
      }

      if (!check_rank_instance_key_invalid(*cfg, update_rank_info.rank_instance_key())) {
        FWPLOGERROR(*owner_, "rank {}:{}:{}:{}, instance_id {}:{}, invalid rank_instance_key",
                    update_rank_info.rank_key().rank_type(), update_rank_info.rank_key().rank_instance_id(),
                    update_rank_info.rank_key().sub_rank_type(), update_rank_info.rank_key().sub_rank_instance_id(),
                    update_rank_info.rank_instance_key().instance_type(),
                    update_rank_info.rank_instance_key().instance_id());
        continue;
      }

      rank_data_index rank_index{
          update_rank_info.rank_key().rank_type(), update_rank_info.rank_key().rank_instance_id(),
          update_rank_info.rank_key().sub_rank_type(), update_rank_info.rank_key().sub_rank_instance_id()};

      // 拉取可以去重,本地模式且非io task添加的拉取则直接patch数据即可
      // local 模式变更数据后也会追加一次数据拉取，这次要通过拉取知道是保持local模式还是切到online
      std::shared_ptr<rank_data_type> rank_data = mutable_rank_data(rank_index, *cfg, logic_rank_user_extend_span{});
      if (!rank_data) {
        FWPLOGERROR(*owner_, "rank {}:{}:{}:{}, instance_id {}:{}, get_rank_data failed",
                    update_rank_info.rank_key().rank_type(), update_rank_info.rank_key().rank_instance_id(),
                    update_rank_info.rank_key().sub_rank_type(), update_rank_info.rank_key().sub_rank_instance_id(),
                    update_rank_info.rank_instance_key().instance_type(),
                    update_rank_info.rank_instance_key().instance_id());
        continue;
      }

      PROJECT_NAMESPACE_ID::DRankInstanceBoard *instance_rank_data =
          mutable_instance_rank_data(rank_data, update_rank_info.rank_instance_key());
      if (!instance_rank_data) {
        FWPLOGERROR(*owner_, "rank {}:{}:{}:{}, instance_id {}:{}, mutable_instance_rank_data failed",
                    update_rank_info.rank_key().rank_type(), update_rank_info.rank_key().rank_instance_id(),
                    update_rank_info.rank_key().sub_rank_type(), update_rank_info.rank_key().sub_rank_instance_id(),
                    update_rank_info.rank_instance_key().instance_type(),
                    update_rank_info.rank_instance_key().instance_id());
        continue;
      }
      if (instance_rank_data->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL &&
          !update_rank_info.trigger_by_io_task()) {
        check_and_settlement_local_rank_data(*cfg, rank_data, update_rank_info.rank_instance_key(),
                                             util::time::time_utility::get_now());
        patch_rank_score_action(ctx, rank_index, *cfg, *rank_data, update_rank_info);
      }
      time_t now = util::time::time_utility::get_now();

      check_and_settlement_local_rank_data(*cfg, rank_data, update_rank_info.rank_instance_key(), now);

      // 如果是local状态，需要再upload一次
      if (instance_rank_data->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL &&
          !update_rank_info.trigger_by_io_task()) {
        // 如果当前榜已被切出，则不需要upload
        if (is_rank_writable_now(*cfg, now)) {
          pending_upload_index_keys.insert(rank_index);
        }
      }
    }
  }
  // 再执行待提交的榜单信息
  using unsubmit_set_type =
      std::pair<google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DRankUnsubmitData>, rank_configure_index>;
  std::unordered_map<rank_data_index, unsubmit_set_type, logic_rank_handle_key_hash> unsubmit_map;
  for (auto &rank_type_data : db_data_) {
    if (!rank_type_data.second) {
      continue;
    }

    if (rank_type_data.second->rank_data.unsubmit_action_size() > 0) {
      auto &dst = unsubmit_map[rank_type_data.first];
      dst.first.Swap(rank_type_data.second->rank_data.mutable_unsubmit_action());
      dst.second = rank_type_data.second->index;
    }
  }

  int32_t ret = 0;
  bool restore_unsubmit = false;
  time_t now = util::time::time_utility::get_now();
  for (auto &unsubmit_type : unsubmit_map) {
    auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(unsubmit_type.second.second.rank_type,
                                                                      unsubmit_type.second.second.rank_instance_id);
    if (!cfg) {
      pending_upload_index_keys.erase(unsubmit_type.first);
      for (auto &unsubmit : unsubmit_type.second.first) {
        // 多次累加之后最终结果可能为0
        if (unsubmit.is_offset() && unsubmit.value() == 0) {
          continue;
        }

        FWPLOGERROR(*owner_, "Try to submmit {},{},{},{} score {}({}) failed, rank cfg ({}, {}) not found.",
                    unsubmit_type.first.get_rank_type(), unsubmit_type.first.get_instance_id(),
                    unsubmit_type.first.get_sub_rank_type(), unsubmit_type.first.get_sub_instance_id(),
                    unsubmit.value(), (unsubmit.is_offset() ? "(offset)" : ""), unsubmit_type.second.second.rank_type,
                    unsubmit_type.second.second.rank_instance_id);
      }
      continue;
    }

    // 不可写则Merge然后等待下一次可写时间
    if (!is_rank_writable_now(*cfg, now)) {
      for (auto &unsubmit : unsubmit_type.second.first) {
        // 多次累加之后最终结果可能为0
        if (unsubmit.is_offset() && unsubmit.value() == 0) {
          continue;
        }

        merge_unsubmit_data(unsubmit_type.first, *cfg, unsubmit);
      }
      continue;
    }
    for (auto &unsubmit : unsubmit_type.second.first) {
      // 多次累加之后最终结果可能为0
      if (unsubmit.is_offset() && unsubmit.value() == 0) {
        continue;
      }

      if (unsubmit.expired_timepoint() != 0 && now > unsubmit.expired_timepoint()) {
        continue;
      }
      // 提交过则不需要再次提交
      bool delay_self_rank_submit = false;
      if (unsubmit.user_key().zone_id() == owner_->get_zone_id() &&
          unsubmit.user_key().user_id() == owner_->get_user_id()) {
        pending_upload_index_keys.erase(unsubmit_type.first);

        auto rank_data = get_rank_data(unsubmit_type.first);
        if (!rank_data) {
          delay_self_rank_submit = true;
        } else {
          auto instance_rank_data = get_instance_rank_data(rank_data, unsubmit.rank_instance_key());
          if (!instance_rank_data) {
            delay_self_rank_submit = true;
          } else if (instance_rank_data->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KONLINE) {
            delay_self_rank_submit = true;
          }
        }
        if (delay_self_rank_submit) {
          // 追加到待拉取队列
          append_pending_update_by_io_task(unsubmit_type.first, *cfg);
          patch_next_io_task_timepoint(now);
        }
      }

      // 如果超时或者被Kill了，剩下的要merge回待执行队列等待下一次重试。
      if (restore_unsubmit && delay_self_rank_submit) {
        merge_unsubmit_data(unsubmit_type.first, *cfg, unsubmit);
      } else {
        int32_t res = RPC_AWAIT_CODE_RESULT(
            submit(ctx, unsubmit_type.first, get_current_rank_data_key(*cfg, unsubmit_type.first.get_sub_instance_id()),
                   *cfg, unsubmit, now));
        if (res < 0) {
          ret = res;
          if (rpc::is_exiting_error_code(res)) {
            restore_unsubmit = true;
          }

          FWPLOGERROR(*owner_, "Try to submmit {},{},{},{} score {}({}) failed, res: {}({}).",
                      unsubmit_type.first.get_rank_type(), unsubmit_type.first.get_instance_id(),
                      unsubmit_type.first.get_sub_rank_type(), unsubmit_type.first.get_sub_instance_id(),
                      unsubmit.value(), (unsubmit.is_offset() ? "(offset)" : ""), res,
                      protobuf_mini_dumper_get_error_msg(res));
        }
      }
    }
  }

  // 不在榜上，强制upload
  now = util::time::time_utility::get_now();
  for (auto &rank_index : pending_upload_index_keys) {
    std::shared_ptr<rank_data_type> rank_data = get_rank_data(rank_index);
    if (!rank_data) {
      continue;
    }

    auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(rank_index.get_rank_type(),
                                                                      rank_index.get_instance_id());
    if (!cfg) {
      continue;
    }

    if (!is_rank_writable_now(*cfg, now)) {
      continue;
    }

    // 只upload当前角色||榜单
  }

  RPC_RETURN_CODE(ret);
}

void user_rank_manager::check_and_settlement_local_rank_data(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg, const std::shared_ptr<rank_data_type> &rank_data,
    ATFW_EXPLICIT_UNUSED_ATTR const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key, time_t now) noexcept {
  if (!rank_data) {
    return;
  }

  for (auto &instance_rank : *rank_data->rank_data.mutable_rank_instance_data()) {
    // 检查本地缓存是否需要结算(不能发奖励，只能执行非增量的扣分逻辑)
    if (now < rank_data->local_mode_next_settlement_timepoint) {
      return;
    }
    auto start_time = logic_rank_get_current_settlement_daily_start_time(cfg, now);
    if (start_time > now) {
      rank_data->local_mode_next_settlement_timepoint = start_time;
    } else {
      rank_data->local_mode_next_settlement_timepoint = start_time + util::time::time_utility::DAY_SECONDS;
    }

    int64_t daily_season_id = logic_rank_get_current_settlement_daily_id(cfg, now);
    if (daily_season_id != instance_rank.local_mode_daily_season_id()) {
      instance_rank.set_local_mode_daily_season_id(logic_rank_get_current_settlement_daily_id(cfg, now));

      if (cfg.content().daily_settlement().local_mode_season_settlement_set_score() < 0) {
        instance_rank.set_last_score_cache(0);
      } else if (cfg.content().daily_settlement().local_mode_season_settlement_set_score() > 0) {
        instance_rank.set_last_score_cache(
            static_cast<uint32_t>(cfg.content().daily_settlement().local_mode_season_settlement_set_score()));
      }
    }

    int64_t custom_season_id = logic_rank_get_current_settlement_custom_season_id(cfg, now);
    if (custom_season_id != instance_rank.local_mode_custom_season_id()) {
      instance_rank.set_local_mode_custom_season_id(logic_rank_get_current_settlement_custom_season_id(cfg, now));

      if (cfg.content().custom_settlement().local_mode_season_settlement_set_score() < 0) {
        instance_rank.set_last_score_cache(0);
      } else if (cfg.content().custom_settlement().local_mode_season_settlement_set_score() > 0) {
        instance_rank.set_last_score_cache(
            static_cast<uint32_t>(cfg.content().custom_settlement().local_mode_season_settlement_set_score()));
      }
    }
  }
}

std::shared_ptr<user_rank_manager::rank_data_type> user_rank_manager::get_rank_data(
    const rank_data_index &rank_index) const noexcept {
  auto iter = db_data_.find(rank_index);
  if (iter == db_data_.end()) {
    return nullptr;
  }

  return iter->second;
}

PROJECT_NAMESPACE_ID::DRankInstanceBoard *user_rank_manager::get_instance_rank_data(
    std::shared_ptr<user_rank_manager::rank_data_type> rank_data,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key) {
  for (auto &instance_rank_data : *rank_data->rank_data.mutable_rank_instance_data()) {
    if (instance_rank_data.rank_instance_key() == rank_instance_key) {
      return &instance_rank_data;
    }
  }
  return nullptr;
}

PROJECT_NAMESPACE_ID::DRankInstanceBoard *user_rank_manager::mutable_instance_rank_data(
    std::shared_ptr<user_rank_manager::rank_data_type> rank_data,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key) {
  for (auto &instance_rank_data : *rank_data->rank_data.mutable_rank_instance_data()) {
    if (instance_rank_data.rank_instance_key() == rank_instance_key) {
      return &instance_rank_data;
    }
  }

  if (rank_instance_key.instance_id() != 0) {
    bool is_exsit = check_custom_key_exsit(rank_instance_key.instance_id());
    if (!is_exsit) {
      FWPLOGERROR(*owner_, "mutable_instance_rank_data error instance {}:{} delete but used",
                  rank_instance_key.instance_type(), rank_instance_key.instance_id());
      return nullptr;
    }
  }

  PROJECT_NAMESPACE_ID::DRankInstanceBoard *res = rank_data->rank_data.mutable_rank_instance_data()->Add();
  if (res) {
    protobuf_copy_message(*res->mutable_rank_instance_key(), rank_instance_key);
    res->set_mode(PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_OFFLINE);
    return res;
  }
  return nullptr;
}

std::shared_ptr<user_rank_manager::rank_data_type> user_rank_manager::mutable_rank_data(
    const rank_data_index &rank_index, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    ATFW_EXPLICIT_UNUSED_ATTR logic_rank_user_extend_span user_extend) noexcept {
  // TODO jijunliang 根据不同的实例类型实现不同的default更新

  std::shared_ptr<rank_data_type> &rank_data = db_data_[rank_index];
  if (!rank_data) {
    rank_data = atfw::memory::stl::make_shared<rank_data_type>();
  }

  // rank_data->mode = cache_mode::kOffline;
  rank_data->index.rank_type = cfg.rank_type();
  rank_data->index.rank_instance_id = cfg.rank_instance_id();
  rank_data->local_mode_next_settlement_timepoint = 0;

  PROJECT_NAMESPACE_ID::DRankKey *rank_key_pb = rank_data->rank_data.mutable_rank_key();
  if (rank_key_pb) {
    rank_key_pb->set_rank_type(rank_index.get_rank_type());
    rank_key_pb->set_rank_instance_id(rank_index.get_instance_id());
    rank_key_pb->set_sub_rank_type(rank_index.get_sub_rank_type());
    rank_key_pb->set_sub_rank_instance_id(rank_index.get_sub_instance_id());
  }

  return rank_data;
}

PROJECT_NAMESPACE_ID::DRankUnsubmitData *user_rank_manager::get_unsubmit_data(const rank_data_index &rank_index,
                                                                              uint32_t target_user_zone_id,
                                                                              uint64_t target_user_id) {
  std::shared_ptr<rank_data_type> rank_data = get_rank_data(rank_index);

  for (int i = 0; i < rank_data->rank_data.unsubmit_action_size(); ++i) {
    PROJECT_NAMESPACE_ID::DRankUnsubmitData *unsubmit = rank_data->rank_data.mutable_unsubmit_action(i);
    if (unsubmit->user_key().zone_id() == target_user_zone_id && unsubmit->user_key().user_id() == target_user_id) {
      return unsubmit;
    }
  }

  return nullptr;
}

PROJECT_NAMESPACE_ID::DRankUnsubmitData *user_rank_manager::mutable_unsubmit_data(
    const rank_data_index &rank_index, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    uint32_t target_user_zone_id, uint64_t target_user_id,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key, logic_rank_user_extend_span user_extend) {
  if (rank_index.get_rank_type() == 0 || target_user_zone_id == 0 || target_user_id == 0) {
    return nullptr;
  }

  std::shared_ptr<rank_data_type> rank_data = mutable_rank_data(rank_index, cfg, user_extend);
  if (!rank_data) {
    return nullptr;
  }

  auto *instance_rank = mutable_instance_rank_data(rank_data, rank_instance_key);
  if (!instance_rank) {
    return nullptr;
  }

  for (int i = 0; i < rank_data->rank_data.unsubmit_action_size(); ++i) {
    PROJECT_NAMESPACE_ID::DRankUnsubmitData *unsubmit = rank_data->rank_data.mutable_unsubmit_action(i);
    if (unsubmit->user_key().zone_id() == target_user_zone_id && unsubmit->user_key().user_id() == target_user_id &&
        unsubmit->rank_instance_key() == rank_instance_key) {
      if (!user_extend.sort_fields.empty() && unsubmit->sort_fields_size() == 0) {
        protobuf_copy_message(*unsubmit->mutable_sort_fields(), user_extend.sort_fields);
      }

      if (!user_extend.ext_fields.empty() && unsubmit->ext_fields_size() == 0) {
        protobuf_copy_message(*unsubmit->mutable_ext_fields(), user_extend.ext_fields);
      }
      return unsubmit;
    }
  }

  PROJECT_NAMESPACE_ID::DRankUnsubmitData *unsubmit = rank_data->rank_data.add_unsubmit_action();
  if (unsubmit != nullptr) {
    unsubmit->mutable_user_key()->set_zone_id(target_user_zone_id);
    unsubmit->mutable_user_key()->set_user_id(target_user_id);
    protobuf_copy_message(*unsubmit->mutable_rank_instance_key(), rank_instance_key);
    // 默认必须是offset
    unsubmit->set_is_offset(true);
  }

  if (!user_extend.sort_fields.empty()) {
    protobuf_copy_message(*unsubmit->mutable_sort_fields(), user_extend.sort_fields);
  }

  if (!user_extend.ext_fields.empty()) {
    protobuf_copy_message(*unsubmit->mutable_ext_fields(), user_extend.ext_fields);
  }

  return unsubmit;
}

void user_rank_manager::merge_unsubmit_data(const rank_data_index &rank_index,
                                            const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                            const PROJECT_NAMESPACE_ID::DRankUnsubmitData &unsubmit) {
  auto now = util::time::time_utility::get_now();
  if (unsubmit.expired_timepoint() != 0 && now > unsubmit.expired_timepoint()) {
    return;
  }

  logic_rank_user_extend_span user_extend;
  user_extend.sort_fields = unsubmit.sort_fields();
  user_extend.ext_fields = unsubmit.ext_fields();

  PROJECT_NAMESPACE_ID::DRankUnsubmitData *ret =
      mutable_unsubmit_data(rank_index, cfg, unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                            unsubmit.rank_instance_key(), user_extend);
  if (ret == nullptr) {
    return;
  }

  if (now > ret->expired_timepoint()) {
    *ret = unsubmit;
    return;
  }

  if (unsubmit.is_offset() == false) {
    if (unsubmit.expired_timepoint() > ret->expired_timepoint()) {
      *ret = unsubmit;
    } else if (ret->is_offset()) {
      ret->set_value(ret->value() + unsubmit.value());
      ret->set_is_offset(false);
    }
  } else {
    ret->set_value(ret->value() + unsubmit.value());
    if (unsubmit.expired_timepoint() > ret->expired_timepoint()) {
      ret->set_expired_timepoint(unsubmit.expired_timepoint());
    }
    if (unsubmit.sort_fields_size() > 0) {
      protobuf_copy_message(*ret->mutable_sort_fields(), unsubmit.sort_fields());
    }
    if (unsubmit.ext_fields_size() > 0) {
      protobuf_copy_message(*ret->mutable_ext_fields(), unsubmit.ext_fields());
    }
  }
}

rpc::result_code_type user_rank_manager::submit(rpc::context &ctx, const rank_data_index &rank_index,
                                                const logic_rank_handle_key &rank_key,
                                                const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                PROJECT_NAMESPACE_ID::DRankUnsubmitData &unsubmit, time_t now) {
  if (unsubmit.expired_timepoint() != 0 && now > unsubmit.expired_timepoint()) {
    RPC_RETURN_CODE(0);
  }

  // 对外的拉取接口总是会等待io任务完成，所以这里不需要考虑提交中的数据未进入cache的问题
  // 更新自己的分数要double check一下排行榜状态，如果是Offline状态要延迟提交
  std::shared_ptr<rank_data_type> my_rank_data;
  PROJECT_NAMESPACE_ID::DRankInstanceBoard *instance_data = nullptr;
  if (unsubmit.user_key().zone_id() == owner_->get_zone_id() &&
      unsubmit.user_key().user_id() == owner_->get_user_id()) {
    my_rank_data = get_rank_data(rank_index);
    if (!my_rank_data) {
      append_pending_update_by_io_task(rank_index, cfg);
      patch_next_io_task_timepoint(now);
      RPC_RETURN_CODE(0);
    }
    instance_data = mutable_instance_rank_data(my_rank_data, unsubmit.rank_instance_key());
    // if (my_role_data->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_OFFLINE) {
    //   append_pending_update_by_io_task(rank_index, cfg);
    //   patch_next_io_task_timepoint(now);
    //   RPC_RETURN_CODE(0);
    // }
  }
  bool force_upload_local = false;
  if (instance_data != nullptr) {
    force_upload_local = my_rank_data && instance_data->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL;
  }
  if (!force_upload_local && unsubmit.is_offset() && unsubmit.value() == 0) {
    RPC_RETURN_CODE(0);
  }

  // 刷新自己的数据缓存
  uint32_t last_score_cache = 0;
  if (my_rank_data && instance_data) {
    // 刷新本地缓存赛季
    if (instance_data->mode() == PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL) {
      check_and_settlement_local_rank_data(cfg, my_rank_data, instance_data->rank_instance_key(), now);
    }
    auto instance_rank = mutable_instance_rank_data(my_rank_data, unsubmit.rank_instance_key());
    if (instance_rank) {
      int64_t new_score;
      if (unsubmit.is_offset()) {
        new_score = static_cast<int64_t>(instance_rank->last_score_cache()) + unsubmit.value();
      } else {
        new_score = unsubmit.value();
      }
      if (new_score < 0) {
        instance_rank->set_last_score_cache(0);
      } else if (new_score > static_cast<uint32_t>(std::numeric_limits<uint32_t>::max())) {
        instance_rank->set_last_score_cache(std::numeric_limits<uint32_t>::max());
      } else {
        instance_rank->set_last_score_cache(static_cast<uint32_t>(new_score));
      }
      if (instance_rank->last_submit_timepoint() < now) {
        instance_rank->set_last_submit_timepoint(now);
      }

      if (unsubmit.sort_fields_size() > 0) {
        protobuf_copy_message(*instance_rank->mutable_sort_fields(), unsubmit.sort_fields());
      }

      if (unsubmit.ext_fields_size() > 0) {
        protobuf_copy_message(*instance_rank->mutable_ext_fields(), unsubmit.ext_fields());
      }
      last_score_cache = instance_rank->last_score_cache();
    }
  }

  // if
  // (owner_->get_user_module_unlock_manager().check_rank_valid(PROJECT_NAMESPACE_ID::config::EN_UNLOCK_MODULE_ID_RANK,
  //                                                               cfg.ban_id()) != 0) {
  //   unsubmit.set_expired_timepoint(-1);
  //   RPC_RETURN_CODE(0);
  // }

  if (!check_rank_report_limit(cfg, unsubmit)) {
    RPC_RETURN_CODE(0);
  }

  logic_rank_handle_variant rank_handle = get_rank_handle(cfg);

  logic_rank_user_extend_span extend_data;
  extend_data.sort_fields = unsubmit.sort_fields();
  extend_data.ext_fields = unsubmit.ext_fields();
  rank_callback_private_data callback_data;
  memset(static_cast<void *>(&callback_data), 0, sizeof(callback_data));
  callback_data.submit_timepoint = now;

  int32_t ret = 0;
  do {
    if (unsubmit.is_offset() == false || force_upload_local) {
      uint32_t submit_value;
      if (force_upload_local) {
        submit_value = last_score_cache;
      } else {
        submit_value = static_cast<uint32_t>(unsubmit.value());
      }
      auto res = RPC_AWAIT_TYPE_RESULT(
          rank_handle.upload_score(ctx, rank_key,
                                   rank_user_key_to_openid(unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                                                           unsubmit.rank_instance_key()),
                                   submit_value, callback_data, extend_data));
      // send_report_rank_oss_log(ctx, unsubmit, rank_key, last_score_cache, res.api_result);

      if (res.api_result != 0) {
        FWPLOGERROR(*owner_,
                    "upload_score to rank ({},{},{},{}) with user={},{}, score={} failed, res: "
                    "{}({})",
                    rank_key.get_rank_type(), rank_key.get_instance_id(), rank_key.get_sub_rank_type(),
                    rank_key.get_sub_instance_id(), unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                    submit_value, res.api_result, protobuf_mini_dumper_get_error_msg(res.api_result));
        ret = res.api_result;
        break;
      }
      FWPLOGDEBUG(*owner_, "upload_score to rank ({},{},{},{}) with user={},{}, score={} success",
                  rank_key.get_rank_type(), rank_key.get_instance_id(), rank_key.get_sub_rank_type(),
                  rank_key.get_sub_instance_id(), unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                  submit_value);

    } else {
      if (unsubmit.value() > 0) {
        uint32_t submit_value = static_cast<uint32_t>(unsubmit.value());
        auto res = RPC_AWAIT_TYPE_RESULT(rank_handle.increase_score(
            ctx, rank_key,
            rank_user_key_to_openid(unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                                    unsubmit.rank_instance_key()),
            submit_value, callback_data, extend_data));
        // send_report_rank_oss_log(ctx, unsubmit, rank_key, last_score_cache, res.api_result);
        if (res.api_result != 0) {
          FWPLOGERROR(*owner_,
                      "increase_score to rank ({},{},{},{}) with user={},{}, score={} failed, res: "
                      "{}({})",
                      rank_key.get_rank_type(), rank_key.get_instance_id(), rank_key.get_sub_rank_type(),
                      rank_key.get_sub_instance_id(), unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                      submit_value, res.api_result, protobuf_mini_dumper_get_error_msg(res.api_result));
          ret = res.api_result;
          break;
        }
        FWPLOGDEBUG(*owner_, "increase_score to rank ({},{},{},{}) with user={},{}, score={} success",
                    rank_key.get_rank_type(), rank_key.get_instance_id(), rank_key.get_sub_rank_type(),
                    rank_key.get_sub_instance_id(), unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                    submit_value);

      } else if (unsubmit.value() < 0) {
        uint32_t submit_value = static_cast<uint32_t>(-unsubmit.value());
        auto res = RPC_AWAIT_TYPE_RESULT(rank_handle.decrease_score(
            ctx, rank_key,
            rank_user_key_to_openid(unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                                    unsubmit.rank_instance_key()),
            submit_value, callback_data, extend_data));
        // send_report_rank_oss_log(ctx, unsubmit, rank_key, last_score_cache, res.api_result);
        if (res.api_result != 0) {
          FWPLOGERROR(*owner_,
                      "decrease_score to rank ({},{},{},{}) with user={},{}, score={} failed, res: "
                      "{}({})",
                      rank_key.get_rank_type(), rank_key.get_instance_id(), rank_key.get_sub_rank_type(),
                      rank_key.get_sub_instance_id(), unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                      submit_value, res.api_result, protobuf_mini_dumper_get_error_msg(res.api_result));
          ret = res.api_result;
          break;
        }
        FWPLOGDEBUG(*owner_, "decrease_score to rank ({},{},{},{}) with user={},{}, score={} success",
                    rank_key.get_rank_type(), rank_key.get_instance_id(), rank_key.get_sub_rank_type(),
                    rank_key.get_sub_instance_id(), unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(),
                    submit_value);
      }
    }
  } while (false);

  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }

  // 设置提交记录过期
  unsubmit.set_expired_timepoint(-1);
  if (my_rank_data) {
    // 提交自己的数据完成后都要刷新一次，以获取最新的排名
    append_pending_update_by_io_task(rank_index, cfg);
    patch_next_io_task_timepoint(now);
  } else {
    // do {
    //   // 提交其他人的数据完成后需要通知对方数据刷新
    //   PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data *async_job =
    //       ctx.create<::PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data>();
    //   PROJECT_NAMESPACE_ID::table_user_async_jobs_blob_data local_async_job;
    //   if (nullptr == async_job) {
    //     async_job = &local_async_job;
    //   }

    //   PROJECT_NAMESPACE_ID::user_async_job_update_rank *job_body = async_job->mutable_update_rank();
    //   if (nullptr == job_body) {
    //     FWLOGERROR("rank {},{},{},{} update score notify for user {},{} but malloc failed", cfg.rank_type(),
    //                cfg.rank_instance_id(), cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(),
    //                unsubmit.user_key().zone_id(), unsubmit.user_key().user_id());

    //     // 通知失败直接忽略即可
    //     break;
    //   }

    //   auto rank_key_pb = job_body->mutable_rank_key();
    //   if (nullptr == rank_key_pb) {
    //     break;
    //   }
    //   rank_key_pb->set_rank_type(cfg.rank_type());
    //   rank_key_pb->set_rank_instance_id(cfg.rank_instance_id());
    //   rank_key_pb->set_sub_rank_type(cfg.content().sub_rank_type());
    //   rank_key_pb->set_sub_rank_instance_id(cfg.content().sub_rank_instance_id());
    //    job_body->set_current_rank_type(rank_key.get_rank_type());
    //   if (unsubmit.is_offset()) {
    //     if (unsubmit.value() < 0) {
    //       job_body->set_add_score(static_cast<uint32_t>(-unsubmit.value()));
    //     } else {
    //       job_body->set_sub_score(static_cast<uint32_t>(unsubmit.value()));
    //     }
    //   } else {
    //     job_body->set_set_score(static_cast<uint32_t>(unsubmit.value()));
    //   }

    //   int32_t res = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::add_jobs(ctx, PROJECT_NAMESPACE_ID::EN_PAJT_NORMAL,
    //                                                                     unsubmit.user_key().user_id(),
    //                                                                     unsubmit.user_key().zone_id(), *async_job));
    //   if (0 != res) {
    //     FWLOGERROR("rank {},{},{},{} update score notify for user {},{} failed, res: {}({})", cfg.rank_type(),
    //                cfg.rank_instance_id(), cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(),
    //                unsubmit.user_key().zone_id(), unsubmit.user_key().user_id(), res,
    //                protobuf_mini_dumper_get_error_msg(res));
    //   }
    // } while (false);
  }

  // TODO(owentou): OSS Log
  RPC_RETURN_CODE(0);
}

void user_rank_manager::patch_next_io_task_timepoint(time_t t) {
  if (0 == io_task_next_timepoint_) {
    io_task_next_timepoint_ = t;
  }

  if (t < io_task_next_timepoint_) {
    io_task_next_timepoint_ = t;
  }
}

void user_rank_manager::update_rank_score_cache(ATFW_EXPLICIT_UNUSED_ATTR rpc::context &ctx,
                                                const rank_data_index &rank_index,
                                                const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                const logic_rank_handle_data &record, time_t now) {
  // 更新本地缓存，必须从排行榜服务拉取到才执行此函数
  std::shared_ptr<rank_data_type> rank_data = mutable_rank_data(rank_index, cfg, logic_rank_user_extend_span());
  if (!rank_data) {
    return;
  }
  PROJECT_NAMESPACE_ID::DRankInstanceKey rank_instance_key;
  rank_instance_key.set_instance_type(record.instance_type);
  rank_instance_key.set_instance_id(record.instance_id);

  PROJECT_NAMESPACE_ID::DRankInstanceBoard *instance_rank = get_instance_rank_data(rank_data, rank_instance_key);
  if (!instance_rank) {
    return;
  }
  instance_rank->set_mode(PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KONLINE);

  instance_rank->set_last_rank_no_cache(record.rank_no);
  instance_rank->set_last_score_cache(record.score);
  if (instance_rank->last_submit_timepoint() < record.timestamp) {
    instance_rank->set_last_submit_timepoint(record.timestamp);
  }

  // 更新本地缓存的赛季ID，防止切回本地模式时重复结算
  instance_rank->set_local_mode_daily_season_id(logic_rank_get_current_settlement_daily_id(cfg, now));
  instance_rank->set_local_mode_custom_season_id(logic_rank_get_current_settlement_custom_season_id(cfg, now));

  auto sort_fields = gsl::make_span(record.extend_data.sort_fields);
  protobuf_copy_message(*instance_rank->mutable_sort_fields(), sort_fields);

  auto ext_fields = gsl::make_span(record.extend_data.ext_fields);
  protobuf_copy_message(*instance_rank->mutable_ext_fields(), ext_fields);

  FWPLOGDEBUG(*owner_, "self rank {},{},{},{} online", cfg.rank_type(), cfg.rank_instance_id(),
              cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id());
}

void user_rank_manager::patch_rank_score_action(
    ATFW_EXPLICIT_UNUSED_ATTR rpc::context &ctx, ATFW_EXPLICIT_UNUSED_ATTR const rank_data_index &rank_key,
    ATFW_EXPLICIT_UNUSED_ATTR const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg, rank_data_type &rank_data,
    const PROJECT_NAMESPACE_ID::user_async_job_update_rank &notify) {
  if (notify.action_hint_case() == PROJECT_NAMESPACE_ID::user_async_job_update_rank::ACTION_HINT_NOT_SET) {
    return;
  }

  if (notify.has_add_score() && notify.add_score() == 0) {
    return;
  }

  if (notify.has_sub_score() && notify.sub_score() == 0) {
    return;
  }

  PROJECT_NAMESPACE_ID::DRankInstanceBoard *instance_rank = nullptr;

  for (auto &instance_rank_data : *rank_data.rank_data.mutable_rank_instance_data()) {
    if (instance_rank_data.rank_instance_key() == notify.rank_instance_key()) {
      instance_rank = &instance_rank_data;
    }
  }
  if (!instance_rank) {
    instance_rank = rank_data.rank_data.mutable_rank_instance_data()->Add();
    protobuf_copy_message(*instance_rank->mutable_rank_instance_key(), notify.rank_instance_key());
  }

  if (instance_rank->mode() != PROJECT_NAMESPACE_ID::EN_RANK_CACHE_MODE_KLOCAL) {
    return;
  }

  switch (notify.action_hint_case()) {
    case PROJECT_NAMESPACE_ID::user_async_job_update_rank::kSetScore:
      instance_rank->set_last_score_cache(notify.set_score());
      instance_rank->set_last_submit_timepoint(util::time::time_utility::get_now());
      break;
    case PROJECT_NAMESPACE_ID::user_async_job_update_rank::kAddScore:
      instance_rank->set_last_score_cache(instance_rank->last_score_cache() + notify.set_score());
      instance_rank->set_last_submit_timepoint(util::time::time_utility::get_now());
      break;
    case PROJECT_NAMESPACE_ID::user_async_job_update_rank::kSubScore:
      instance_rank->set_last_score_cache(instance_rank->last_score_cache() - notify.set_score());
      instance_rank->set_last_submit_timepoint(util::time::time_utility::get_now());
      break;
    default:
      break;
  }
}

void user_rank_manager::append_pending_update_by_io_task(const rank_data_index &rank_index,
                                                         const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg) {
  append_pending_update_without_action(rank_index, cfg, true);
}

void user_rank_manager::append_pending_update_without_action(const rank_data_index &rank_index,
                                                             const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
                                                             bool trigger_by_io_task) {
  do {
    auto index_iter = pending_update_no_action_index_.find(rank_index);
    if (index_iter == pending_update_no_action_index_.end()) {
      break;
    }

    if (cfg.is_instance_rank() && get_default_role_guid() == 0) {
      // 角色榜单 当前无角色
      return;
    }

    // 添加过则忽略
    if (index_iter->second == trigger_by_io_task) {
      return;
    }
    // 添加过非IO任务发起的更新，且当前是IO发起的也则忽略。IO任务发起的有去重操作，属于更低优先级
    if (trigger_by_io_task == true && index_iter->second == false) {
      return;
    }
  } while (false);
  pending_update_no_action_index_[rank_index] = trigger_by_io_task;

  // TODO 全部发起
  PROJECT_NAMESPACE_ID::user_async_job_update_rank pending_job;
  auto rank_key_pb = pending_job.mutable_rank_key();
  if (nullptr == rank_key_pb) {
    return;
  }

  rank_key_pb->set_rank_type(rank_index.get_rank_type());
  rank_key_pb->set_rank_instance_id(rank_index.get_instance_id());
  rank_key_pb->set_sub_rank_type(rank_index.get_sub_rank_type());
  rank_key_pb->set_sub_rank_instance_id(rank_index.get_sub_instance_id());

  pending_job.set_trigger_by_io_task(trigger_by_io_task);
  if (!cfg.is_instance_rank()) {
    pending_job.mutable_rank_instance_key()->set_instance_type(
        PROJECT_NAMESPACE_ID::RankInstanceType::EN_RANK_INSTANCE_TYPE_USER);
    // pending_job.mutable_rank_instance_key()->set_instance_id(owner_->get_user_id());
  } else {
    // instance_rank 根据逻辑在处理
  }

  pending_update_score_ranks_.emplace_back(std::move(pending_job));
}

void user_rank_manager::append_pending_update(const PROJECT_NAMESPACE_ID::user_async_job_update_rank &rank_job) {
  do {
    if (rank_job.action_hint_case() != PROJECT_NAMESPACE_ID::user_async_job_update_rank::ACTION_HINT_NOT_SET) {
      break;
    }

    auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(rank_job.rank_key().rank_type(),
                                                                      rank_job.rank_key().rank_instance_id());
    if (!cfg) {
      FWPLOGWARNING(*owner_, "rank cfg {},{} not found, ignore job", rank_job.rank_key().rank_type(),
                    rank_job.rank_key().rank_instance_id());
      return;
    }

    rank_data_index rank_index{*cfg};
    auto index_iter = pending_update_no_action_index_.find(rank_index);
    if (index_iter == pending_update_no_action_index_.end()) {
      pending_update_no_action_index_[rank_index] = rank_job.trigger_by_io_task();
      break;
    }
    // 添加过则忽略
    if (index_iter->second == rank_job.trigger_by_io_task()) {
      return;
    }
    // 添加过非IO任务发起的更新，且当前是IO发起的也则忽略。IO任务发起的有去重操作，属于更低优先级
    if (rank_job.trigger_by_io_task() == true && index_iter->second == false) {
      return;
    }

    pending_update_no_action_index_[rank_index] = rank_job.trigger_by_io_task();
  } while (false);

  pending_update_score_ranks_.push_back(rank_job);
  patch_next_io_task_timepoint(util::time::time_utility::get_now());
}

void user_rank_manager::convert_to(PROJECT_NAMESPACE_ID::DRankUserBasicData &output,
                                   const logic_rank_handle_data &input) {
  output.mutable_user_key()->set_zone_id(static_cast<uint32_t>(input.zone_id));
  output.mutable_user_key()->set_user_id(input.user_id);
  output.set_rank_no(input.rank_no);
  output.set_score(input.score);
  output.set_submit_timepoint(input.timestamp);

  auto rank_instance_key = output.mutable_rank_instance_key();
  rank_instance_key->set_instance_type(input.instance_type);
  rank_instance_key->set_instance_id(input.instance_id);

  protobuf_copy_message(*output.mutable_sort_fields(), gsl::make_span(input.extend_data.sort_fields));
  protobuf_copy_message(*output.mutable_ext_fields(), gsl::make_span(input.extend_data.ext_fields));
}

void user_rank_manager::submit_rank_score_no_wait(
    rpc::context &ctx, const std::vector<PROJECT_NAMESPACE_ID::user_rank_unsubmit_data> &need_submit_vec) {
  FWLOGDEBUG("start submit_rank_score_no_wait");
  auto user_ptr = owner_->shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, "user_rank_manager.submit_rank_score_no_wait",
      [user_ptr, need_submit_vec](rpc::context &child_ctx) -> rpc::result_code_type {
        user_rank_manager &self = user_ptr->get_user_rank_manager();
        int32_t ret = 0;
        for (const auto &unit : need_submit_vec) {
          // if (unit.is_offset() == 0) {
          //   continue;
          // }
          auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(unit.rank_key().rank_type(),
                                                                            unit.rank_key().rank_instance_id());
          if (cfg == nullptr) {
            FWPLOGERROR(*user_ptr, "player update rank failed, not found rank {}:{} setting, score {}",
                        unit.rank_key().rank_type(), unit.rank_key().rank_instance_id(), unit.value());
            continue;
          }

          if (!unit.is_offset() && unit.user_key().user_id() == user_ptr->get_user_id() &&
              unit.user_key().zone_id() == user_ptr->get_zone_id()) {
            ret = RPC_AWAIT_CODE_RESULT(self.set_rank_score(child_ctx, logic_rank_handle_key{*cfg}, *cfg,
                                                            unit.rank_instance_key(),
                                                            static_cast<uint32_t>(unit.value())));
          } else if (unit.value() > 0) {
            ret = RPC_AWAIT_CODE_RESULT(self.add_rank_score(
                child_ctx, logic_rank_handle_key{*cfg}, *cfg, unit.user_key().zone_id(), unit.user_key().user_id(),
                unit.rank_instance_key(), static_cast<uint32_t>(unit.value())));
          } else {
            ret = RPC_AWAIT_CODE_RESULT(self.sub_rank_score(
                child_ctx, logic_rank_handle_key{*cfg}, *cfg, unit.user_key().zone_id(), unit.user_key().user_id(),
                unit.rank_instance_key(), static_cast<uint32_t>(-unit.value())));
          }
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FWPLOGERROR(*owner_, "async_invoke a rank io task failed.res: {}({})", *invoke_result.get_error(),
                protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
  return;
}

void user_rank_manager::update_rank_cache(
    const std::vector<PROJECT_NAMESPACE_ID::user_rank_unsubmit_data> &update_vec) {
  for (const auto &unit : update_vec) {
    auto cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(unit.rank_key().rank_type(),
                                                                      unit.rank_key().rank_instance_id());
    if (cfg == nullptr) {
      continue;
    }
    auto rank_data = get_rank_data(rank_data_index{*cfg});
    if (rank_data == nullptr) {
      continue;
    }
    auto role_data = get_instance_rank_data(rank_data, unit.rank_instance_key());
    if (role_data == nullptr) {
      continue;
    }
    if (unit.value() > 0) {
      role_data->set_last_score_cache(role_data->last_score_cache() + static_cast<uint32_t>(unit.value()));
    } else {
      role_data->set_last_score_cache(role_data->last_score_cache() - static_cast<uint32_t>(unit.value()));
    }
  }
}

void user_rank_manager::delete_instance_rank_cache(const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key) {
  for (auto &rank_data : db_data_) {
    if (rank_data.second == nullptr) {
      continue;
    }
    auto &role_data = *rank_data.second->rank_data.mutable_rank_instance_data();
    auto iter = role_data.begin();
    while (iter != role_data.end()) {
      if (iter->rank_instance_key() == rank_instance_key) {
        role_data.erase(iter);
        break;
      }
      ++iter;
    }
  }
  return;
}

void user_rank_manager::delete_rank_cache(const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rule) {
  rank_data_index rank_key{rule};
  db_data_.erase(rank_key);
  return;
}

void user_rank_manager::delete_instance_rank_data(const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key) {
  std::vector<PROJECT_NAMESPACE_ID::DRankKey> wait_clear_rank;
  for (auto &rank_data : db_data_) {
    if (rank_data.second == nullptr) {
      continue;
    }
    auto &role_data = *rank_data.second->rank_data.mutable_rank_instance_data();
    auto iter = role_data.begin();
    while (iter != role_data.end()) {
      if (iter->rank_instance_key() == rank_instance_key) {
        PROJECT_NAMESPACE_ID::DRankKey unit;
        unit.set_rank_type(rank_data.first.get_rank_type());
        unit.set_rank_instance_id(rank_data.first.get_instance_id());
        // unit.mutable_user_key()->set_user_id(owner_->get_user_id());
        // unit.mutable_user_key()->set_zone_id(owner_->get_zone_id());
        // unit.set_role_guid(role_guid);
        // unit.set_is_offset(false);
        // unit.set_value(0);
        wait_clear_rank.push_back(unit);
        role_data.erase(iter);
        break;
      }
      ++iter;
    }
  }

  auto user_ptr = owner_->shared_from_this();
  auto invoke_result = rpc::async_invoke(
      "user_rank_manager", "delete_instance_rank_data",
      [user_ptr, wait_clear_rank, rank_instance_key](rpc::context &child_ctx) -> rpc::result_code_type {
        user_rank_manager &self = user_ptr->get_user_rank_manager();
        int32_t ret = 0;
        for (const auto &unit : wait_clear_rank) {
          ret = RPC_AWAIT_CODE_RESULT(
              self.clear_instance_rank(child_ctx, unit.rank_type(), unit.rank_instance_id(), rank_instance_key));
          if (ret != 0) {
            FWPLOGERROR(*user_ptr, "clear_user_one_rank failed, rank {}:{}", unit.rank_type(), unit.rank_instance_id());
          }
        }
        RPC_RETURN_CODE(ret);
      });
  if (invoke_result.is_error()) {
    FWPLOGERROR(*owner_, "async_invoke a rank io task failed.res: {}({})", *invoke_result.get_error(),
                protobuf_mini_dumper_get_error_msg(*invoke_result.get_error()));
  }
  return;
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_rank_manager::clear_user_all_rank(rpc::context &ctx) {
  auto rank_cfgs = excel::get_ExcelRankRule_all_of_rank_type_rank_instance_id();
  int32_t ret = 0;
  for (const auto &rank_cfg : rank_cfgs) {
    if (rank_cfg.second == nullptr) {
      continue;
    }
    ret = RPC_AWAIT_CODE_RESULT(clear_user_one_rank(ctx, *rank_cfg.second));
    if (ret != 0) {
      break;
    }
  }
  RPC_RETURN_CODE(ret);
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_rank_manager::clear_user_one_rank(rpc::context &ctx,
                                                                                     uint32_t rank_type,
                                                                                     uint32_t rank_instance_id) {
  auto rank_cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(rank_type, rank_instance_id);
  if (nullptr == rank_cfg) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_INVALID_TYPE);
  }
  int32_t ret = RPC_AWAIT_CODE_RESULT(clear_user_one_rank(ctx, *rank_cfg));
  if (ret != 0) {
    FWPLOGERROR(*owner_, "clear_user_one_rank failed, type {}, id {}", rank_cfg->rank_type(),
                rank_cfg->rank_instance_id());
  }
  RPC_RETURN_CODE(ret);
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_rank_manager::clear_user_one_rank(
    rpc::context &ctx, const PROJECT_NAMESPACE_ID::config::ExcelRankRule &rank_cfg) {
  rank_ret_t ret;
  for (const auto &unit : db_data_) {
    if (unit.first.get_rank_type() != rank_cfg.rank_type() ||
        unit.first.get_instance_id() != rank_cfg.rank_instance_id()) {
      continue;
    }
    for (const auto &instance_rank_data : unit.second->rank_data.rank_instance_data()) {
      if (instance_rank_data.last_rank_no_cache() != 0 || instance_rank_data.last_score_cache() != 0) {
        logic_rank_handle_variant rank_handle = get_rank_handle(rank_cfg);
        std::string rank_openid = rank_user_key_to_openid(owner_->get_zone_id(), owner_->get_user_id(),
                                                          instance_rank_data.rank_instance_key());
        logic_rank_handle_key rank_key{rank_cfg};
        ret = RPC_AWAIT_CODE_RESULT(rank_handle.clear_special_one(ctx, rank_key, rank_openid));

        if (ret.api_result != 0) {
          RPC_RETURN_CODE(ret.api_result);
        }
      }
    }
  }

  RPC_RETURN_CODE(0);
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type user_rank_manager::clear_instance_rank(
    rpc::context &ctx, uint32_t rank_type, uint32_t rank_instance_id,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key) {
  auto rank_cfg = excel::get_ExcelRankRule_by_rank_type_rank_instance_id(rank_type, rank_instance_id);
  if (nullptr == rank_cfg) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_INVALID_TYPE);
  }

  std::string rank_openid = rank_user_key_to_openid(owner_->get_zone_id(), owner_->get_user_id(), rank_instance_key);
  rank_ret_t ret;

  logic_rank_handle_variant rank_handle = get_rank_handle(*rank_cfg);

  logic_rank_handle_key rank_key{*rank_cfg};
  ret = RPC_AWAIT_CODE_RESULT(rank_handle.clear_special_one(ctx, rank_key, rank_openid));

  if (ret.api_result != 0) {
    RPC_RETURN_CODE(ret.api_result);
  }

  RPC_RETURN_CODE(0);
}

rpc::result_void_type user_rank_manager::set_client_rank_cache_expired(rpc::context &ctx) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::SCRankCacheExpiredSync> sync_body{ctx};
  rpc::lobbysvrclientservice::send_rank_cache_expired_sync(ctx, *sync_body, *owner_->get_session());
  RPC_RETURN_VOID;
}

int32_t user_rank_manager::get_default_role_guid() {
  // TODO jijunliang : 目前先返回用户的默认角色GUID，后续如果有多角色需求再改
  return 0;
}

bool user_rank_manager::check_custom_key_exsit(ATFW_EXPLICIT_UNUSED_ATTR int64_t guid) { return true; }

bool user_rank_manager::check_rank_report_limit(
    ATFW_EXPLICIT_UNUSED_ATTR const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    ATFW_EXPLICIT_UNUSED_ATTR PROJECT_NAMESPACE_ID::DRankUnsubmitData &unsubmit) {
  return true;
}

bool user_rank_manager::check_rank_instance_key_invalid(
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule &cfg,
    const PROJECT_NAMESPACE_ID::DRankInstanceKey &rank_instance_key) {
  switch (rank_instance_key.instance_type()) {
    case PROJECT_NAMESPACE_ID::EN_RANK_INSTANCE_TYPE_USER: {
      return true;
    }
    case PROJECT_NAMESPACE_ID::EN_RANK_INSTANCE_TYPE_ROLE: {
      // TODO jijunliang : 检测玩家角色和榜单类型是否匹配
      return true;
    }
    default: {
      FWPLOGERROR(*owner_, "rank instance key invalid, rank_type {}, rank_instance_id {}, instance_type {}:{}",
                  cfg.rank_type(), cfg.rank_instance_id(), rank_instance_key.instance_type(),
                  rank_instance_key.instance_id());
      return false;
    }
  }
  return false;
}