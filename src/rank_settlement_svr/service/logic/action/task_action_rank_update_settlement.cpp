#include "logic/action/task_action_rank_update_settlement.h"

#include <log/log_wrapper.h>
#include <random/random_generator.h>
#include <time/time_utility.h>

#include <config/excel_config_rank_index.h>
#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <data/player_key_hash_helper.h>

#include <dispatcher/task_manager.h>

#include <rpc/rpc_async_invoke.h>

#include <rank_logic/logic_rank_algorithm.h>
#include <rank_logic/logic_rank_handle.h>

#include <rpc/db/local_db_interface.h>
#include <rpc/rank_board/rank.h>

#include <assert.h>
#include <algorithm>
#include <memory>
#include <unordered_set>

#include "logic/action/task_action_rank_send_settlement.h"
#include "logic/rank_settlement_manager.h"

task_action_rank_update_settlement::task_action_rank_update_settlement(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param) {
  start_timepoint_ = util::time::time_utility::now();
}

task_action_rank_update_settlement::~task_action_rank_update_settlement() {}

task_action_rank_send_settlement::result_type task_action_rank_update_settlement::operator()() {
  uint32_t settle_loop_count =
      logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_settlement_cfg>().settle_loop_count();
  if (settle_loop_count < 1 || settle_loop_count > logic_config::me()->get_server_cfg().rank().query_max_count()) {
    settle_loop_count = logic_config::me()->get_server_cfg().rank().query_max_count();
  }

  // lock configure group, configure may be reload during settlement
  auto group = excel::config_manager::me()->get_current_config_group();
  if (!group) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // foreach rank
  for (auto& rank_rule : group->ExcelRankRule.get_all_of_rank_type_rank_instance_id()) {
    bool allow_continue = true;
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    check_trigger_exit(get_shared_context(), allow_continue, current_task_status);

    // 如果是task级别的禁止继续，则要强行中断
    if (!allow_continue) {
      break;
    }

    if (!rank_rule.second) {
      continue;
    }

    RPC_AWAIT_IGNORE_RESULT(
        process_rank(get_shared_context(), allow_continue, group, *rank_rule.second, settle_loop_count));
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_rank_update_settlement::on_complete() {
  if (task_type_trait::get_task_id(rank_settlement_manager::me()->update_task_) == get_task_id()) {
    task_type_trait::reset_task(rank_settlement_manager::me()->update_task_);
  }

  auto end_timepoint = util::time::time_utility::now();
  FWLOGINFO("[STATISTICS]: task_action_rank_update_settlement run {}ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(end_timepoint - start_timepoint_).count());

  std::chrono::system_clock::duration update_offset = task_manager::make_timeout_duration(
      logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_settlement_cfg>().settle_interval());
  if (update_offset <= update_offset.zero()) {
    update_offset = task_manager::make_timeout_duration(logic_config::me()->get_cfg_task().nomsg().timeout());
  }

  if (update_offset < std::chrono::seconds{60}) {
    update_offset = task_manager::make_timeout_duration(std::chrono::seconds{180});
  }

  util::random::xoshiro256_starstar rnd;
  uint64_t seed =
      logic_config::me()->get_local_server_id() ^ static_cast<uint64_t>(util::time::time_utility::get_now());
  rnd.init_seed(static_cast<util::random::xoshiro256_starstar::result_type>(seed));

  // 随机下一个启动时间点[update_offset*0.75, update_offset*1.25)
  update_offset = update_offset - (update_offset / 4) +
                  std::chrono::system_clock::duration{
                      rnd.random_between<std::chrono::system_clock::duration::rep>(0, update_offset.count() >> 1)};

  rank_settlement_manager::me()->reset_update_protect(util::time::time_utility::now() + update_offset);
  return get_result();
}

rpc::result_code_type task_action_rank_update_settlement::await_all(
    rpc::context& ctx, const std::vector<task_type_trait::task_type>& tasks) {
  int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(ctx, tasks));
  if (res < 0) {
    RPC_RETURN_CODE(res);
  }

  // 所有的都成功才返回true
  for (auto& task : tasks) {
    if (!task_type_trait::empty(task) && task_type_trait::get_result(task) < 0) {
      RPC_RETURN_CODE(task_type_trait::get_result(task));
    }
  }

  RPC_RETURN_CODE(0);
}

void task_action_rank_update_settlement::check_trigger_exit(rpc::context& /*ctx*/, bool& allow_continue,
                                                            task_type_trait::task_status current_status) {
  auto now = util::time::time_utility::now();
  // loop - 即将超时，先退出等下一次启动再继续
  if (now + std::chrono::seconds{3} >= param_.timeout) {
    allow_continue = false;
  }
  // loop - ranksvr正在退出，中断掉等下一次启动再继续
  if (rank_settlement_manager::me()->is_exiting()) {
    allow_continue = false;
  }

  // 任务正在退出，可能是收到外部kill指令也要退出循环
  if (task_type_trait::is_exiting(current_status)) {
    allow_continue = false;
  }
}

static rpc::rpc_result<int64_t> fetch_rank_total_count(rpc::context& ctx, logic_rank_handle_variant& rank_handle,
                                                       const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg,
                                                       int64_t mirror_id) {
  PROJECT_NAMESPACE_ID::DRankImageData image;
  image.set_mirror_id(mirror_id);
  auto res = RPC_AWAIT_TYPE_RESULT(rank_handle.get_top_rank(ctx, logic_rank_handle_key{cfg}, 1, 1, &image));
  if (res.api_result != 0) {
    FWLOGERROR("fetch_rank_total_count {},{}-{},{},{},{} failed, res: {}({})", rank_handle.get_world_id(),
               rank_handle.get_zone_id(), cfg.rank_type(), cfg.rank_instance_id(), cfg.content().sub_rank_type(),
               cfg.content().sub_rank_instance_id(), res.api_result,
               protobuf_mini_dumper_get_error_msg(res.api_result));

    RPC_RETURN_TYPE(res.api_result);
  }

  RPC_RETURN_TYPE(static_cast<int64_t>(rank_handle.get_current_total_count()));
}

static rpc::result_void_type refresh_new_peried(
    rpc::context& ctx, logic_rank_handle_variant& rank_handle, const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg,
    bool has_daily_reword, time_t daily_settlement_id, bool has_custom_reword, time_t custom_settlement_id,
    PROJECT_NAMESPACE_ID::table_rank_settlement_blob_data& rank_settle_db_data) {
  // 任意排名到了新的结算周期都要重新触发结算
  if (!(has_daily_reword && daily_settlement_id > rank_settle_db_data.settle_daily_day_no()) &&
      !(has_custom_reword && custom_settlement_id > rank_settle_db_data.settle_custom_season_no())) {
    RPC_RETURN_VOID;
  }

  int64_t rank_total_count =
      RPC_AWAIT_TYPE_RESULT(fetch_rank_total_count(ctx, rank_handle, cfg, rank_settle_db_data.mirror_id()));
  if (rank_total_count < 0) {
    RPC_RETURN_VOID;
  }

  // 重置状态，下面会刷新的
  rank_settle_db_data.set_latest_settlement_rank(static_cast<uint32_t>(rank_total_count));
  rank_settle_db_data.set_current_settle_server_id(0);
  rank_settle_db_data.set_current_settle_timeout(0);

  // 排行版空的就直接保存并跳过
  if (has_daily_reword && daily_settlement_id > rank_settle_db_data.settle_daily_day_no()) {
    rank_settle_db_data.set_latest_settlement_need_daily(true);
    FWLOGINFO("rank {},{}-{},{},{},{} start to settle for day {}", rank_handle.get_world_id(),
              rank_handle.get_zone_id(), cfg.rank_type(), cfg.rank_instance_id(), cfg.content().sub_rank_type(),
              cfg.content().sub_rank_instance_id(), daily_settlement_id);
  } else {
    rank_settle_db_data.set_latest_settlement_need_daily(false);
  }

  if (has_custom_reword && custom_settlement_id > rank_settle_db_data.settle_custom_season_no()) {
    rank_settle_db_data.set_latest_settlement_need_custom(true);
    FWLOGINFO("rank {},{}-{},{},{},{} start to settle for custom day {}", rank_handle.get_world_id(),
              rank_handle.get_zone_id(), cfg.rank_type(), cfg.rank_instance_id(), cfg.content().sub_rank_type(),
              cfg.content().sub_rank_instance_id(), custom_settlement_id);
  } else {
    rank_settle_db_data.set_latest_settlement_need_custom(false);
  }

  rank_settle_db_data.set_settle_daily_day_no(daily_settlement_id);
  rank_settle_db_data.set_settle_custom_season_no(custom_settlement_id);

  RPC_RETURN_VOID;
}

rpc::result_code_type task_action_rank_update_settlement::settle_rank_once(
    rpc::context& ctx, bool& allow_continue,
    const ::excel::excel_config_type_traits::shared_ptr<excel::config_group_t>& group,
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg, logic_rank_handle_variant& rank_handle,
    uint32_t settle_loop_count, bool /*has_daily_reword*/, time_t /*daily_settlement_id*/, bool /*has_custom_reword*/,
    time_t /*custom_settlement_id*/,
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>& rank_settlement_dbdata,
    uint64_t& rank_settlement_dbversion) {
  PROJECT_NAMESPACE_ID::table_rank_settlement_blob_data& rank_settle_db_data =
      *rank_settlement_dbdata->mutable_blob_data();
  if (rank_settle_db_data.latest_settlement_rank() <= 0) {
    allow_continue = false;
    RPC_RETURN_CODE(0);
  }

  if (!allow_continue) {
    RPC_RETURN_CODE(0);
  }

  rank_settle_db_data.set_current_settle_server_id(logic_config::me()->get_local_server_id());
  rank_settle_db_data.set_current_settle_timeout(std::chrono::system_clock::to_time_t(param_.timeout) + 1);

  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_settlement::replace(
      ctx, rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>(ctx, rank_settlement_dbdata),
      rank_settlement_dbversion));
  if (ret < 0) {
    FWLOGERROR("Set rank ({},{}) settlement lock data failed, ret: {}({})", rank_settlement_dbdata->zone_id(),
               rank_settlement_dbdata->rank_type(), ret, protobuf_mini_dumper_get_error_msg(ret));

    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    check_trigger_exit(ctx, allow_continue, current_task_status);
    RPC_RETURN_CODE(ret);
  }

  // try to settle rank
  uint32_t next_settlement_rank;
  uint32_t pull_start_no = static_cast<uint32_t>(rank_settle_db_data.latest_settlement_rank());
  uint32_t pull_count = settle_loop_count;
  if (pull_start_no > settle_loop_count) {
    // 保护性初始赋值，防止流程死循环
    next_settlement_rank = pull_start_no - settle_loop_count;
    pull_start_no = next_settlement_rank + 1;
  } else {
    // 保护性初始赋值，防止流程死循环
    next_settlement_rank = 0;
    pull_start_no = 1;
    pull_count = static_cast<uint32_t>(rank_settle_db_data.latest_settlement_rank());
  }

  PROJECT_NAMESPACE_ID::DRankImageData image;
  image.set_mirror_id(rank_settle_db_data.mirror_id());
  auto res = RPC_AWAIT_TYPE_RESULT(
      rank_handle.get_top_rank(ctx, logic_rank_handle_key{cfg}, pull_start_no, pull_count, &image));

  if (res.api_result != 0) {
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    check_trigger_exit(ctx, allow_continue, current_task_status);
    FWLOGERROR(
        "get_top_rank {},{},{},{} start: {} count: {} "
        "failed, res: {}({})",
        cfg.rank_type(), cfg.rank_instance_id(), cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(),
        pull_start_no, pull_count, res.api_result, protobuf_mini_dumper_get_error_msg(res.api_result));
    RPC_RETURN_CODE(res.api_result);
  }
  {
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    if (task_type_trait::is_exiting(current_task_status)) {
      allow_continue = false;
      RPC_RETURN_CODE(0);
    }
  }

  rank_handle.reset_cursor_back();

  // loop - foreach user - start settlement task
  std::vector<task_type_trait::task_type> await_tasks;
  await_tasks.reserve(rank_handle.get_current_count());
  for (bool need_next = true; need_next; need_next = rank_handle.previous_cursor()) {
    if (!rank_handle.valid_cursor()) {
      continue;
    }
    uint32_t current_score = rank_handle.get_current_score();
    uint32_t current_no = rank_handle.get_current_no();
    // 分数为0的不发奖（功能未解锁分数也一直是0）
    if (current_score <= 0) {
      // 刷新下一次结算排名,前面排过序，所以这里一定是递减的
      next_settlement_rank = current_no > 0 ? current_no - 1 : 0;
      continue;
    }

    task_action_rank_send_settlement::ctor_param_t subtask_param;
    subtask_param.group = group;
    subtask_param.rank_rule_cfg = &cfg;
    std::tie(subtask_param.zone_id, subtask_param.user_id, subtask_param.instance_type, subtask_param.instance_id) =
        rank_openid_to_user_key(rank_handle.get_current_open_id());
    // 如果采用ABC三榜轮切策略，则设置copyto和reward的type
    subtask_param.score = current_score;
    subtask_param.rank_no = current_no;
    subtask_param.settle_rank_no = current_no;
    subtask_param.mirror_id = rank_settle_db_data.mirror_id();
    subtask_param.sort_fields.reserve(rank_handle.get_current_sort_fields().size());
    subtask_param.sort_fields.assign(rank_handle.get_current_sort_fields().begin(),
                                     rank_handle.get_current_sort_fields().end());
    subtask_param.ext_fields.reserve(rank_handle.get_current_ext_fields().size());
    subtask_param.ext_fields.assign(rank_handle.get_current_ext_fields().begin(),
                                    rank_handle.get_current_ext_fields().end());
    if (rank_settle_db_data.latest_settlement_need_daily()) {
      subtask_param.daily_settlement_pool_id = cfg.content().daily_settlement().rank_reward_pool_id();
    } else {
      subtask_param.daily_settlement_pool_id = 0;
    }
    subtask_param.daily_settlement_pool_type = cfg.content().daily_settlement().reward_type();
    subtask_param.daily_settlement_day_id = rank_settle_db_data.settle_daily_day_no();
    if (rank_settle_db_data.latest_settlement_need_custom()) {
      subtask_param.custom_settlement_pool_id = cfg.content().custom_settlement().rank_reward_pool_id();
    } else {
      subtask_param.custom_settlement_pool_id = 0;
    }
    subtask_param.custom_settlement_pool_type = cfg.content().custom_settlement().reward_type();
    subtask_param.custom_settlement_season_id = rank_settle_db_data.settle_custom_season_no();
    subtask_param.save_history =
        (cfg.content().daily_settlement().save_history() || cfg.content().custom_settlement().save_history());

    subtask_param.caller_context = &ctx;

    task_type_trait::task_type task_inst;
    task_manager::me()->create_task<task_action_rank_send_settlement>(task_inst, std::move(subtask_param));

    if (task_type_trait::empty(task_inst)) {
      FWLOGERROR("create task_action_rank_send_settlement failed");
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
      break;
    } else {
      task_type_trait::task_type subtask = task_inst;

      dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

      ret = task_manager::me()->start_task(task_inst, start_data);
      if (ret < 0) {
        FWLOGERROR(
            "start task_action_rank_send_settlement failed, "
            "ret: {}({})",
            ret, protobuf_mini_dumper_get_error_msg(ret));
        break;
      } else {
        // 刷新下一次结算排名,前面排过序，所以这里一定是递减的
        next_settlement_rank = current_no > 0 ? current_no - 1 : 0;

        if (!task_type_trait::empty(subtask)) {
          await_tasks.push_back(subtask);
        }
      }
    }
  }

  FWLOGINFO(
      "Await for rank {},{}-{},{},{},{} settlement for {}, "
      "latest_settlement_rank: {}, await_tasks size {} ",
      rank_handle.get_world_id(), rank_handle.get_zone_id(), cfg.rank_type(), cfg.rank_instance_id(),
      cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), next_settlement_rank,
      rank_settlement_dbdata->blob_data().latest_settlement_rank(), await_tasks.size());
  // 等待并发执行的结算任务
  ret = RPC_AWAIT_CODE_RESULT(await_all(ctx, await_tasks));
  if (ret == 0) {
    // 准备下一轮结算流程
    rank_settlement_dbdata->mutable_blob_data()->set_latest_settlement_rank(next_settlement_rank);
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type task_action_rank_update_settlement::cleanup_save(
    rpc::context& ctx, bool& allow_continue, const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg,
    logic_rank_handle_variant& rank_handle, uint32_t settle_loop_count,
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>& rank_settlement_dbdata,
    uint64_t& rank_settlement_dbversion, bool& hold_optimistic_lock) {
  PROJECT_NAMESPACE_ID::table_rank_settlement_blob_data& rank_settle_db_data =
      *rank_settlement_dbdata->mutable_blob_data();

  time_t now = util::time::time_utility::get_now();
  rank_settle_db_data.set_current_settle_server_id(logic_config::me()->get_local_server_id());

  bool ready_to_cleanup = false;

  if (ready_to_cleanup) {
    rank_settle_db_data.set_current_settle_timeout(std::chrono::system_clock::to_time_t(param_.timeout) + 1);
  } else {
    // 提前中断以减少乐观锁抢占
    if (!hold_optimistic_lock) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_COMMON_BREAK);
    }
    rank_settle_db_data.set_current_settle_timeout(now + 1);
  }

  // final save progress
  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_settlement::replace(
      ctx, rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>(ctx, rank_settlement_dbdata),
      rank_settlement_dbversion));
  if (ret < 0) {
    FWLOGERROR("Reset rank ({}-{},{},{},{}) settlement lock data failed, ret: {}({})",
               rank_settlement_dbdata->zone_id(), cfg.rank_type(), cfg.rank_instance_id(),
               cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
    TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
    check_trigger_exit(ctx, allow_continue, current_task_status);
    RPC_RETURN_CODE(ret);
  }
  hold_optimistic_lock = true;

  // 不需要走清理流程的话break即可
  if (!ready_to_cleanup) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_COMMON_BREAK);
  }

  // 准备清理奖励榜

  rank_callback_private_data callback_data;
  memset(&callback_data, 0, sizeof(callback_data));
  callback_data.submit_timepoint = util::time::time_utility::get_now();

  ret = PROJECT_NAMESPACE_ID::err::EN_COMMON_BREAK;
  std::unordered_set<PROJECT_NAMESPACE_ID::DPlayerIDKey, player_key_hash_t, player_key_equal_t> prev_round_user_keys;
  prev_round_user_keys.reserve(settle_loop_count);
  size_t repeated_count = 0;
  for (bool loop_again = true; loop_again && repeated_count < 2 * settle_loop_count;) {
    {
      TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
      check_trigger_exit(ctx, allow_continue, current_task_status);
    }
    if (!allow_continue) {
      break;
    }

    // cleanup scores
    auto res = RPC_AWAIT_TYPE_RESULT(rank_handle.get_top_rank(ctx, logic_rank_handle_key(cfg), 1, settle_loop_count));
    if (res.api_result != 0) {
      FWLOGERROR("rank_get_top_rank {},{}-{},{},{},{}({}) for {} failed, res: {}({})", rank_handle.get_world_id(),
                 rank_handle.get_zone_id(), cfg.rank_type(), cfg.rank_instance_id(), cfg.content().sub_rank_type(),
                 cfg.content().sub_rank_instance_id(), 1, settle_loop_count, res.api_result,
                 protobuf_mini_dumper_get_error_msg(res.api_result));

      TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
      check_trigger_exit(ctx, allow_continue, current_task_status);
      loop_again = false;
      continue;
    }

    // 清理完毕
    rank_handle.reset_cursor_front();
    if (rank_handle.get_current_score() <= 0) {
      rank_settle_db_data.set_bind_activity_id(0);
      loop_again = false;

      // 外层再重试一次，执行final save（缩短锁的时间）
      ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
      continue;
    }

    rank_handle.reset_cursor_back();

    // save_process_again = false;
    uint32_t cleanup_max_score = 0;
    uint32_t cleanup_min_score = UINT32_MAX;
    size_t cleanup_count = 0;
    std::unordered_set<PROJECT_NAMESPACE_ID::DPlayerIDKey, player_key_hash_t, player_key_equal_t> cur_round_user_keys;
    cur_round_user_keys.reserve(settle_loop_count);
    for (bool need_next = true; need_next; need_next = rank_handle.previous_cursor()) {
      if (!rank_handle.valid_cursor()) {
        continue;
      }

      uint32_t current_score = rank_handle.get_current_score();
      auto openid = rank_handle.get_current_open_id();
      if (current_score > 0) {
        uint32_t user_zone_id = 0;
        uint64_t user_id = 0;
        int64_t instance_type = 0;
        int64_t instance_id = 0;

        std::tie(user_zone_id, user_id, instance_type, instance_id) = rank_openid_to_user_key(openid);
        PROJECT_NAMESPACE_ID::DPlayerIDKey user_key;
        user_key.set_zone_id(user_zone_id);
        user_key.set_user_id(user_id);
        cur_round_user_keys.insert(user_key);
        if (prev_round_user_keys.end() != prev_round_user_keys.find(user_key)) {
          ++repeated_count;
        }

        ++cleanup_count;

        if (cleanup_max_score < current_score) {
          cleanup_max_score = current_score;
        }
        if (cleanup_min_score > current_score) {
          cleanup_min_score = current_score;
        }

        res =
            RPC_AWAIT_TYPE_RESULT(rank_handle.upload_score(ctx, logic_rank_handle_key(cfg), openid, 0, callback_data));
        if (res.api_result != 0) {
          FWLOGERROR("rank_upload_top_rank_score {},{}-{},{},{},{} for {} failed, res: {}({})",
                     rank_handle.get_world_id(), rank_handle.get_zone_id(), cfg.rank_type(), cfg.rank_instance_id(),
                     cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), openid, res.api_result,
                     protobuf_mini_dumper_get_error_msg(res.api_result));
          loop_again = false;
          break;
        }
      }
    }

    prev_round_user_keys.swap(cur_round_user_keys);

    FWLOGINFO("cleanup rank {},{}-{},{},{},{} cleanup max score: {}, min score: {}, count: {}",
              rank_handle.get_world_id(), rank_handle.get_zone_id(), cfg.rank_type(), cfg.rank_instance_id(),
              cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), cleanup_max_score, cleanup_min_score,
              cleanup_count);
  }

  RPC_RETURN_CODE(ret);
}

static rpc::result_void_type query_mirror_create(
    rpc::context& ctx, const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg,
    rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>& rank_settle_db_data,
    uint64_t& rank_settlement_dbversion) {
  int64_t mirror_id = 0;
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank_key{ctx};
  rank_key->set_rank_type(cfg.rank_type());
  rank_key->set_rank_instance_id(cfg.rank_instance_id());
  rank_key->set_sub_rank_type(cfg.content().sub_rank_type());
  rank_key->set_sub_rank_instance_id(cfg.content().sub_rank_instance_id());
  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::rank_board::make_new_mirror(ctx, *rank_key, mirror_id));
  if (ret != 0) {
    FWLOGERROR("rank {}:{}:{}:{}, creat rank mirror failed ret:{}", rank_key->rank_type(), rank_key->rank_instance_id(),
               rank_key->sub_rank_type(), rank_key->sub_rank_instance_id(), ret);
    RPC_RETURN_VOID;
  }

  // 保存镜像id
  rank_settle_db_data->mutable_blob_data()->set_mirror_id(mirror_id);

  ret = RPC_AWAIT_CODE_RESULT(rpc::db::rank_settlement::replace(
      ctx, rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>(ctx, rank_settle_db_data),
      rank_settlement_dbversion));
  if (ret < 0) {
    FWLOGERROR("Set rank ({},{},{},{}) settlement lock data failed, ret: {}({})", rank_settle_db_data->zone_id(),
               rank_settle_db_data->rank_type(), rank_settle_db_data->sub_rank_type(),
               rank_settle_db_data->sub_rank_instance_id(), ret, protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_VOID;
}

rpc::result_code_type task_action_rank_update_settlement::process_rank(
    rpc::context& ctx, bool& allow_continue,
    const ::excel::excel_config_type_traits::shared_ptr<excel::config_group_t>& group,
    const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg, uint32_t settle_loop_count) {
  // ============ 判定协程任务健康状态 ============
  if (!allow_continue) {
    RPC_RETURN_CODE(0);
  }

  // ============ 判定是否有需要执行的actions流程 ============

  if (cfg.content().settlement_type() != PROJECT_NAMESPACE_ID::EN_RANK_SETTLEMENT_TYPE_IMAGE &&
      cfg.content().settlement_type() != PROJECT_NAMESPACE_ID::EN_RANK_SETTLEMENT_TYPE_ORIGINAL_AND_CLEAR) {
    RPC_RETURN_CODE(0);
  }

  bool has_daily_reword = logic_rank_has_rank_daily_reward(cfg);
  bool has_custom_reword = logic_rank_has_rank_custom_reward(cfg);

  time_t now = util::time::time_utility::get_now();
  // 还未开始
  if (now < cfg.content().valid_time().begin_time().seconds()) {
    RPC_RETURN_CODE(0);
  }

  // 已经结束超过一个周期。（留一个周期作为结算时间）
  if (cfg.content().valid_time().end_time().seconds() > 0) {
    if (has_custom_reword) {
      std::pair<time_t, time_t> expired_time = logic_rank_get_final_settlement_custom_expired_time(cfg);
      if (now >= expired_time.second) {
        RPC_RETURN_CODE(0);
      }
    } else if (has_daily_reword) {
      if (now >= cfg.content().valid_time().end_time().seconds() + util::time::time_utility::DAY_SECONDS) {
        RPC_RETURN_CODE(0);
      }
    }
  }

  int64_t daily_settlement_id = logic_rank_get_current_settlement_daily_id(cfg, now);
  int64_t custom_settlement_id = logic_rank_get_current_settlement_custom_season_id(cfg, now);

  // 容忍值，榜切换期间暂停一会再开始结算
  time_t time_tolerate = logic_config::me()->get_const_settings().time_tolerate();
  if (time_tolerate <= 0) {
    time_tolerate = 15;
  }

  // 时间容忍值
  if (logic_rank_get_current_settlement_daily_id(cfg, now - time_tolerate) != daily_settlement_id ||
      logic_rank_get_current_settlement_custom_season_id(cfg, now - time_tolerate) != custom_settlement_id) {
    RPC_RETURN_CODE(0);
  }

  uint32_t zone_id = logic_rank_get_global_rank_shared_zone_id(cfg);
  if (zone_id == 0) {
    zone_id = logic_config::me()->get_local_zone_id();
  }
  // pull rank settlement data
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement> rank_settlement_dbdata(ctx);
  uint64_t rank_settlement_dbversion = 0;
  int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::rank_settlement::get_all(
      ctx, zone_id, cfg.rank_type(), cfg.rank_instance_id(), cfg.content().sub_rank_type(),
      cfg.content().sub_rank_instance_id(), rank_settlement_dbdata, rank_settlement_dbversion));
  if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    FWLOGERROR("Fetch rank {}-{},{},{},{} settlement data failed, res: {}({})", zone_id, cfg.rank_type(),
               cfg.rank_instance_id(), cfg.content().sub_rank_type(), cfg.content().sub_rank_instance_id(), res,
               protobuf_mini_dumper_get_error_msg(res));
    RPC_RETURN_CODE(res);
  }

  rank_settlement_dbdata->set_zone_id(zone_id);
  rank_settlement_dbdata->set_rank_type(cfg.rank_type());
  rank_settlement_dbdata->set_rank_instance_id(cfg.rank_instance_id());
  rank_settlement_dbdata->set_sub_rank_type(cfg.content().sub_rank_type());
  rank_settlement_dbdata->set_sub_rank_instance_id(cfg.content().sub_rank_instance_id());

  PROJECT_NAMESPACE_ID::table_rank_settlement_blob_data& rank_settle_db_data =
      *rank_settlement_dbdata->mutable_blob_data();

  // ============ 独占判定流程 ============
  // skip rank settlement at another ranksvr and not expired
  if (rank_settle_db_data.current_settle_server_id() != 0 &&
      rank_settle_db_data.current_settle_server_id() != logic_config::me()->get_local_server_id() &&
      now <= rank_settle_db_data.current_settle_timeout()) {
    RPC_RETURN_CODE(0);
  }

  logic_rank_handle_variant rank_handle{logic_config::me()->get_local_world_id(), zone_id, cfg};

  bool hold_optimistic_lock = false;
  do {
    // skip rank not need settlement
    if (0 == rank_settle_db_data.latest_settlement_rank() &&
        (!has_daily_reword || rank_settle_db_data.settle_daily_day_no() >= daily_settlement_id) &&
        (!has_custom_reword || rank_settle_db_data.settle_custom_season_no() >= custom_settlement_id)) {
      break;
    }

    if (rank_settle_db_data.mirror_id() == 0 && logic_rank_is_self_rank(cfg)) {
      // 还没有镜像id, 先去请求进行拉取 等待下一次结算
      RPC_AWAIT_IGNORE_VOID(query_mirror_create(ctx, cfg, rank_settlement_dbdata, rank_settlement_dbversion));
      break;
    }

    // ============ 新周期流程 ============
    RPC_AWAIT_IGNORE_VOID(refresh_new_peried(ctx, rank_handle, cfg, has_daily_reword, daily_settlement_id,
                                             has_custom_reword, custom_settlement_id, rank_settle_db_data));

    // ============ 发奖流程 ============
    for (size_t retry_times = 0; rank_settle_db_data.latest_settlement_rank() > 0 && retry_times < 3;) {
      TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
      check_trigger_exit(ctx, allow_continue, current_task_status);

      if (!allow_continue) {
        break;
      }

      res = RPC_AWAIT_CODE_RESULT(settle_rank_once(
          ctx, allow_continue, group, cfg, rank_handle, settle_loop_count, has_daily_reword, daily_settlement_id,
          has_custom_reword, custom_settlement_id, rank_settlement_dbdata, rank_settlement_dbversion));
      if (0 != res) {
        ++retry_times;
      } else {
        hold_optimistic_lock = true;
        if (!allow_continue) {
          res = RPC_AWAIT_CODE_RESULT(rpc::db::rank_settlement::replace(
              ctx, rpc::clone_shared_message<PROJECT_NAMESPACE_ID::table_rank_settlement>(ctx, rank_settlement_dbdata),
              rank_settlement_dbversion));
          if (res < 0) {
            FWLOGERROR("Set rank ({},{}) settlement lock data failed, res: {}({})", rank_settlement_dbdata->zone_id(),
                       rank_settlement_dbdata->rank_type(), res, protobuf_mini_dumper_get_error_msg(res));
          }
        }
      }
      // 请求限频
      // if (!rank_settlement_manager::me()->rank_settlement_frequency_check()) {
      //   allow_continue = false;
      //   break;
      // }
    }
  } while (false);

  TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
  check_trigger_exit(ctx, allow_continue, current_task_status);

  // 一直重试到清理过程异常或结束
  while (allow_continue && 0 == RPC_AWAIT_CODE_RESULT(cleanup_save(ctx, allow_continue, cfg, rank_handle,
                                                                   settle_loop_count, rank_settlement_dbdata,
                                                                   rank_settlement_dbversion, hold_optimistic_lock))) {
  }

  RPC_RETURN_CODE(0);
}
