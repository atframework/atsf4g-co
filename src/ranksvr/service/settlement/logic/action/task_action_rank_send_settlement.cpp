#include "logic/action/task_action_rank_send_settlement.h"

#include <log/log_wrapper.h>
#include <random/random_generator.h>
#include <time/time_utility.h>

#include <config/excel_config_const_index.h>
#include <config/excel_config_rank_index.h>
#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/async_jobs/async_jobs.h>
#include <rpc/db/local_db_interface.h>

#include <rank/logic_rank_algorithm.h>
#include <rank/logic_rank_handle.h>

#include <utility/rank_util.h>

#include <assert.h>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

#include "logic/rank_settlement_manager.h"

task_action_rank_send_settlement::task_action_rank_send_settlement(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param) {}

task_action_rank_send_settlement::~task_action_rank_send_settlement() {}

task_action_rank_send_settlement::result_type task_action_rank_send_settlement::operator()() {
  uint32_t zone_id = logic_rank_get_global_rank_shared_zone_id(*param_.rank_rule_cfg);
  if (zone_id == 0) {
    zone_id = logic_config::me()->get_local_zone_id();
  }
  std::string user_openid =
      rank_user_key_to_openid(param_.zone_id, param_.user_id, param_.instance_type, param_.instance_id);

  logic_rank_handle_variant rank_handle{logic_config::me()->get_local_world_id(), zone_id, *param_.rank_rule_cfg};

  int64_t sub_score = 0;
  int64_t set_score = 0;
  int32_t score_change_type = 0;

  auto ret = RPC_AWAIT_CODE_RESULT(
      settle_daily_rewards(get_shared_context(), rank_handle, user_openid, sub_score, set_score, score_change_type));
  if (ret < 0) {
    TASK_ACTION_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(
      settle_custom_rewards(get_shared_context(), rank_handle, user_openid, sub_score, set_score, score_change_type));
  if (ret < 0) {
    TASK_ACTION_RETURN_CODE(ret);
  }

  ret = RPC_AWAIT_CODE_RESULT(
      settle_special_rule(get_shared_context(), rank_handle, user_openid, sub_score, set_score, score_change_type));
  if (ret < 0) {
    TASK_ACTION_RETURN_CODE(ret);
  }

  FWLOGDEBUG("task_action_rank_send_settlement for user {}:{}, rank {}:{}, sub_score: {}, set_score: {}",
             param_.zone_id, param_.user_id, param_.rank_rule_cfg->rank_type(),
             param_.rank_rule_cfg->rank_instance_id(), sub_score, set_score);
  if (sub_score > 0 || set_score != 0) {
    ret = RPC_AWAIT_CODE_RESULT(
        update_sub_period_score(get_shared_context(), rank_handle, user_openid, sub_score, set_score));
  }

  // 历史存量落地
  if (param_.save_history) {
    int32_t res = RPC_AWAIT_CODE_RESULT(save_history(get_shared_context(), rank_handle));
    if (res < 0) {
      ret = res;
    }
    // TODO jijunliang 历史数据日志
  }

  TASK_ACTION_RETURN_CODE(ret);
}

rpc::result_code_type task_action_rank_send_settlement::settle_daily_rewards(rpc::context& ctx,
                                                                             logic_rank_handle_variant& /*rank_handle*/,
                                                                             const std::string& user_openid,
                                                                             int64_t& sub_score, int64_t& set_score,
                                                                             int32_t& score_change_type) {
  if (param_.daily_settlement_pool_id == 0) {
    RPC_RETURN_CODE(0);
  }
  auto daily_reward_cfg = excel::get_current_rank_settle_rewards(
      param_.daily_settlement_pool_id, param_.daily_settlement_pool_type, param_.settle_rank_no, param_.score);

  if (!daily_reward_cfg) {
    RPC_RETURN_CODE(0);
  }

  score_change_type = daily_reward_cfg->content().score_change_type();

  if (daily_reward_cfg->content().score_change_type() == PROJECT_NAMESPACE_ID::EN_RANK_SCORE_SETTLE_RESET_SCORE) {
    if (daily_reward_cfg->content().score_change_param() >= 0) {
      set_score = daily_reward_cfg->content().score_change_param();
    }
  }

  if (daily_reward_cfg->content().score_change_type() == PROJECT_NAMESPACE_ID::EN_RANK_SCORE_SETTLE_RESET_SCORE) {
    if (daily_reward_cfg->content().score_change_param() < 0) {
      sub_score = std::numeric_limits<int64_t>::max();
    } else {
      sub_score += daily_reward_cfg->content().score_change_param();
    }
    if (daily_reward_cfg->content().reward_size() <= 0) {
      RPC_RETURN_CODE(0);
    }
  }
  rpc::shared_message<::PROJECT_NAMESPACE_ID::user_async_jobs_blob_data> async_job{get_shared_context()};

  PROJECT_NAMESPACE_ID::user_async_job_settle_rank* job_body = async_job->mutable_settle_rank();
  if (nullptr == job_body) {
    FWLOGERROR(
        "rank {},{},{},{}(pood_id={}) settle daily reward for user {},{} with rank={} score={} settle_rank={} but "
        "malloc failed",
        param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
        param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
        param_.daily_settlement_pool_id, param_.zone_id, param_.user_id, param_.rank_no, param_.settle_rank_no,
        param_.score);
    RPC_RETURN_CODE(0);
  }

  do {
    auto rank_data = job_body->mutable_rank_board_basic_data();
    if (nullptr == rank_data) {
      break;
    }

    rank_data->set_rank_no(param_.rank_no);
    rank_data->set_settle_rank_no(param_.settle_rank_no);
    rank_data->mutable_rank_instance_key()->set_instance_type(param_.instance_type);
    rank_data->mutable_rank_instance_key()->set_instance_id(param_.instance_id);

    rank_data->set_score(param_.score);
    if (!param_.sort_fields.empty()) {
      rank_data->mutable_sort_fields()->Reserve(static_cast<int>(param_.sort_fields.size()));
      for (auto& field : param_.sort_fields) {
        rank_data->add_sort_fields(field);
      }
    }
    if (!param_.ext_fields.empty()) {
      rank_data->mutable_ext_fields()->Reserve(static_cast<int>(param_.ext_fields.size()));
      for (auto& field : param_.ext_fields) {
        rank_data->add_ext_fields(field);
      }
    }

    auto rank_key = rank_data->mutable_rank_key();
    if (nullptr == rank_key) {
      break;
    }
    rank_key->set_rank_type(param_.rank_rule_cfg->rank_type());
    rank_key->set_rank_instance_id(param_.rank_rule_cfg->rank_instance_id());
    rank_key->set_sub_rank_type(param_.rank_rule_cfg->content().sub_rank_type());
    rank_key->set_sub_rank_instance_id(param_.rank_rule_cfg->content().sub_rank_instance_id());
  } while (false);

  job_body->set_rank_reward_pool_id(param_.daily_settlement_pool_id);
  job_body->set_rank_reward_pool_type(param_.daily_settlement_pool_type);
  job_body->set_save_history(param_.save_history);
  job_body->set_daily_reward_cycle_no(param_.daily_settlement_day_id);

  int32_t res = RPC_AWAIT_CODE_RESULT(
      rpc::async_jobs::add_jobs(ctx, PROJECT_NAMESPACE_ID::EN_PAJT_NORMAL, param_.user_id, param_.zone_id, async_job));
  if (0 != res) {
    FWLOGERROR(
        "rank {},{},{},{}(pood_id={}) settle daily reward for user {},{} with rank={} score={} settle_rank_no={} "
        "failed, res: "
        "{}({})",
        param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
        param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
        param_.daily_settlement_pool_id, param_.zone_id, param_.user_id, param_.rank_no, param_.settle_rank_no,
        param_.score, res, protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }
  FWLOGDEBUG(
      "Send rank {},{},{},{}(pood_id={}) daily settlement type={} with score={}, rank_no={} settle_rank_no={} to "
      "user "
      "{}:{}:{}:{}",
      param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
      param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
      param_.daily_settlement_pool_id, static_cast<int32_t>(param_.daily_settlement_pool_type), param_.score,
      param_.rank_no, param_.settle_rank_no, param_.zone_id, param_.user_id, param_.instance_type, param_.instance_id);

  // TODO jijunliang 每日奖励日志

  RPC_RETURN_CODE(0);
}

rpc::result_code_type task_action_rank_send_settlement::settle_custom_rewards(
    rpc::context& ctx, logic_rank_handle_variant& /*rank_handle*/, const std::string& user_openid, int64_t& sub_score,
    int64_t& set_score, int32_t& score_change_type) {
  // 调用RPC给玩家发送排行版奖励结算通知
  if (param_.custom_settlement_pool_id == 0) {
    RPC_RETURN_CODE(0);
  }

  auto custom_reward_cfg = excel::get_current_rank_settle_rewards(
      param_.custom_settlement_pool_id, param_.custom_settlement_pool_type, param_.settle_rank_no, param_.score);

  if (!custom_reward_cfg) {
    RPC_RETURN_CODE(0);
  }

  score_change_type = custom_reward_cfg->content().score_change_type();
  if (custom_reward_cfg->content().score_change_type() == PROJECT_NAMESPACE_ID::EN_RANK_SCORE_SETTLE_RESET_SCORE) {
    if (custom_reward_cfg->content().score_change_param() != 0) {
      set_score = custom_reward_cfg->content().score_change_param();
    }
  }
  if (custom_reward_cfg->content().score_change_type() == PROJECT_NAMESPACE_ID::EN_RANK_SCORE_SETTLE_RESET_SCORE) {
    if (custom_reward_cfg->content().score_change_param() < 0) {
      sub_score = std::numeric_limits<int64_t>::max();
    } else {
      if (std::numeric_limits<int64_t>::max() - custom_reward_cfg->content().score_change_param() >= sub_score) {
        sub_score += custom_reward_cfg->content().score_change_param();
      } else {
        sub_score = std::numeric_limits<int64_t>::max();
      }
    }
  }
  if (custom_reward_cfg->content().reward_size() <= 0) {
    RPC_RETURN_CODE(0);
  }

  rpc::shared_message<::PROJECT_NAMESPACE_ID::user_async_jobs_blob_data> async_job{get_shared_context()};

  PROJECT_NAMESPACE_ID::user_async_job_settle_rank* job_body = async_job->mutable_settle_rank();
  if (nullptr == job_body) {
    FWLOGERROR(
        "rank {},{},{},{}(pood_id={}) settle custom reward for user {},{} with rank={} score={} settle_rank_no={} "
        "but "
        "malloc "
        "failed",
        param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
        param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
        param_.custom_settlement_pool_id, param_.zone_id, param_.user_id, param_.rank_no, param_.settle_rank_no,
        param_.score);
    RPC_RETURN_CODE(0);
  }

  do {
    auto rank_data = job_body->mutable_rank_board_basic_data();
    if (nullptr == rank_data) {
      break;
    }

    rank_data->set_rank_no(param_.rank_no);
    rank_data->set_settle_rank_no(param_.settle_rank_no);

    rank_data->set_score(param_.score);
    rank_data->mutable_rank_instance_key()->set_instance_type(param_.instance_type);
    rank_data->mutable_rank_instance_key()->set_instance_id(param_.instance_id);

    if (!param_.sort_fields.empty()) {
      rank_data->mutable_sort_fields()->Reserve(static_cast<int>(param_.sort_fields.size()));
      for (auto& field : param_.sort_fields) {
        rank_data->add_sort_fields(field);
      }
    }
    if (!param_.ext_fields.empty()) {
      rank_data->mutable_ext_fields()->Reserve(static_cast<int>(param_.ext_fields.size()));
      for (auto& field : param_.ext_fields) {
        rank_data->add_ext_fields(field);
      }
    }

    auto rank_key = rank_data->mutable_rank_key();
    if (nullptr == rank_key) {
      break;
    }
    rank_key->set_rank_type(param_.rank_rule_cfg->rank_type());
    rank_key->set_rank_instance_id(param_.rank_rule_cfg->rank_instance_id());
    rank_key->set_sub_rank_type(param_.rank_rule_cfg->content().sub_rank_type());
    rank_key->set_sub_rank_instance_id(param_.rank_rule_cfg->content().sub_rank_instance_id());
  } while (false);

  job_body->set_rank_reward_pool_id(param_.custom_settlement_pool_id);
  job_body->set_rank_reward_pool_type(param_.custom_settlement_pool_type);
  job_body->set_save_history(param_.save_history);
  job_body->set_custom_reward_cycle_no(param_.custom_settlement_season_id);

  int32_t res = RPC_AWAIT_CODE_RESULT(
      rpc::async_jobs::add_jobs(ctx, PROJECT_NAMESPACE_ID::EN_PAJT_NORMAL, param_.user_id, param_.zone_id, async_job));
  if (0 != res) {
    FWLOGERROR(
        "rank {},{},{},{}(pood_id={}) settle custom reward for user {},{} with rank={} score={} settle_rank_no={} "
        "failed, res: "
        "{}({})",
        param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
        param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
        param_.custom_settlement_pool_id, param_.zone_id, param_.user_id, param_.rank_no, param_.settle_rank_no,
        param_.score, res, protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }
  FWLOGDEBUG(
      "Send rank {},{},{},{}(pood_id={}) custom settlement rewark type={} with score={}, rank_no={}  "
      "settle_rank_no={} "
      " to user "
      "{}:{}:{}:{}",
      param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
      param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
      param_.custom_settlement_pool_id, static_cast<int32_t>(param_.custom_settlement_pool_id), param_.score,
      param_.rank_no, param_.settle_rank_no, param_.zone_id, param_.user_id, param_.instance_type, param_.instance_id);

  // TODO jijunliang 自定义奖励日志

  RPC_RETURN_CODE(0);
}

rpc::result_code_type task_action_rank_send_settlement::update_sub_period_score(rpc::context& ctx,
                                                                                logic_rank_handle_variant& rank_handle,
                                                                                const std::string& user_openid,
                                                                                int64_t sub_score, int64_t set_score) {
  rank_callback_private_data callback_data;
  memset(&callback_data, 0, sizeof(callback_data));
  callback_data.submit_timepoint = util::time::time_utility::get_now();

  // 优先执行重置分数
  if (set_score != 0) {
    sub_score = static_cast<int64_t>(param_.score) - set_score;
  } else {
    if (sub_score > static_cast<int64_t>(param_.score)) {
      sub_score = static_cast<int64_t>(param_.score);
    }
  }

  if (sub_score == 0) {
    RPC_RETURN_CODE(0);
  }

  rank_ret_t res;
  if (sub_score < 0) {
    res =
        RPC_AWAIT_TYPE_RESULT(rank_handle.increase_score(ctx, logic_rank_handle_key(*param_.rank_rule_cfg), user_openid,
                                                         static_cast<uint32_t>(-sub_score), callback_data));
  } else if (sub_score > 0) {
    res =
        RPC_AWAIT_TYPE_RESULT(rank_handle.decrease_score(ctx, logic_rank_handle_key(*param_.rank_rule_cfg), user_openid,
                                                         static_cast<uint32_t>(sub_score), callback_data));
  }

  if (res.api_result != 0) {
    FWLOGERROR("rank {},{},{},{}, pood_id={}) sub score {} for user {},{} failed, res: {}({})",
               param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
               param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
               param_.custom_settlement_pool_id, sub_score, param_.zone_id, param_.user_id, res.api_result,
               protobuf_mini_dumper_get_error_msg(res.api_result));
    RPC_RETURN_CODE(0);
  } else {
    FWLOGINFO("rank {},{},{},{}(pood_id={}) sub score {} for user {},{} success", param_.rank_rule_cfg->rank_type(),
              param_.rank_rule_cfg->rank_instance_id(), param_.rank_rule_cfg->content().sub_rank_type(),
              param_.rank_rule_cfg->content().sub_rank_instance_id(), param_.custom_settlement_pool_id, sub_score,
              param_.zone_id, param_.user_id);
  }

  rpc::shared_message<::PROJECT_NAMESPACE_ID::user_async_jobs_blob_data> async_job{get_shared_context()};

  PROJECT_NAMESPACE_ID::user_async_job_update_rank* job_body = async_job->mutable_update_rank();
  if (nullptr == job_body) {
    FWLOGERROR("rank {},{},{},{}(pood_id={}) update score notify for user {},{} but malloc failed",
               param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
               param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
               param_.custom_settlement_pool_id, param_.zone_id, param_.user_id);
    RPC_RETURN_CODE(0);
  }

  do {
    auto rank_key = job_body->mutable_rank_key();
    if (nullptr == rank_key) {
      break;
    }
    rank_key->set_rank_type(param_.rank_rule_cfg->rank_type());
    rank_key->set_rank_instance_id(param_.rank_rule_cfg->rank_instance_id());
    rank_key->set_sub_rank_type(param_.rank_rule_cfg->content().sub_rank_type());
    rank_key->set_sub_rank_instance_id(param_.rank_rule_cfg->content().sub_rank_instance_id());
    if (sub_score < 0) {
      job_body->set_add_score(static_cast<uint32_t>(-sub_score));
    } else {
      job_body->set_sub_score(static_cast<uint32_t>(sub_score));
    }

    int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::async_jobs::add_jobs(ctx, PROJECT_NAMESPACE_ID::EN_PAJT_NORMAL,
                                                                  param_.user_id, param_.zone_id, async_job));
    if (0 != ret) {
      FWLOGERROR("rank {},{},{},{}(pood_id={}) update score notify for user {},{} failed, ret: {}({})",
                 param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
                 param_.rank_rule_cfg->content().sub_rank_type(),
                 param_.rank_rule_cfg->content().sub_rank_instance_id(), param_.custom_settlement_pool_id,
                 param_.zone_id, param_.user_id, ret, protobuf_mini_dumper_get_error_msg(ret));
    }
  } while (false);

  RPC_RETURN_CODE(0);
}

rpc::result_code_type task_action_rank_send_settlement::save_history(rpc::context& ctx,
                                                                     logic_rank_handle_variant& /*rank_handle*/) {
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_history> table{ctx};

  table->set_zone_id(param_.zone_id);
  table->set_rank_type(param_.rank_rule_cfg->rank_type());
  table->set_rank_instance_id(param_.rank_rule_cfg->rank_instance_id());
  table->set_sub_rank_type(param_.rank_rule_cfg->content().sub_rank_type());
  table->set_sub_rank_instance_id(param_.rank_rule_cfg->content().sub_rank_instance_id());

  table->set_rank_day_id(param_.daily_settlement_day_id);
  table->set_rank_season_id(param_.custom_settlement_season_id);
  table->set_rank_no(param_.rank_no);
  table->set_rank_score(param_.score);
  table->set_settle_rank_no(param_.settle_rank_no);

  table->set_user_id(param_.user_id);
  table->set_user_zone_id(param_.zone_id);

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_history::replace(ctx, table.clone(ctx)));
  if (ret < 0) {
    FWLOGERROR(
        "rank {},{},{},{}(pool_id={}) save season (day_id: {}, season_id: {}) history for user {},{}(rank_no: {}, "
        "score: {} settle_rank_no:{}) failed, res: {}({})",
        param_.rank_rule_cfg->rank_type(), param_.rank_rule_cfg->rank_instance_id(),
        param_.rank_rule_cfg->content().sub_rank_type(), param_.rank_rule_cfg->content().sub_rank_instance_id(),
        param_.custom_settlement_pool_id, param_.daily_settlement_day_id, param_.custom_settlement_season_id,
        param_.zone_id, param_.user_id, param_.rank_no, param_.score, param_.settle_rank_no, ret,
        protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type task_action_rank_send_settlement::settle_special_rule(rpc::context& ctx,
                                                                            logic_rank_handle_variant& /*rank_handle*/,
                                                                            const std::string& /*user_openid*/,
                                                                            int64_t& sub_score, int64_t& /*set_score*/,
                                                                            int32_t& score_change_type) {
  // 判断是否是特殊清理
  if (logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_settlement_cfg>().disable_rank_settlemnet_clear()) {
    RPC_RETURN_CODE(0);
  }
  RPC_RETURN_CODE(0);
}