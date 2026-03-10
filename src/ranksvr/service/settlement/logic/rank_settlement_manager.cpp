#include "logic/rank_settlement_manager.h"

#include <log/log_wrapper.h>
#include <random/random_generator.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

#include "logic/action/task_action_rank_update_settlement.h"

rank_settlement_manager::rank_settlement_manager()
    : is_exiting_(false), next_update_timepoint_(std::chrono::system_clock::from_time_t(0)) {}

int rank_settlement_manager::init() {
  std::chrono::system_clock::duration update_offset =
      task_manager::make_timeout_duration(logic_config::me()->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_settlement_cfg>().settle_interval());

  if (update_offset <= std::chrono::system_clock::duration::zero()) {
    update_offset = task_manager::make_timeout_duration(logic_config::me()->get_cfg_task().nomsg().timeout());
  }

  if (update_offset > std::chrono::system_clock::duration::zero()) {
    util::random::xoshiro256_starstar rnd;
    uint64_t seed =
        logic_config::me()->get_local_server_id() ^ static_cast<uint64_t>(util::time::time_utility::get_now());
    rnd.init_seed(static_cast<util::random::xoshiro256_starstar::result_type>(seed));

    update_offset = std::chrono::system_clock::duration{
        rnd.random_between<std::chrono::system_clock::duration::rep>(0, update_offset.count())};
    next_update_timepoint_ = util::time::time_utility::now() + update_offset;
  }
  return 0;
}

int rank_settlement_manager::tick() {
  int ret = 0;
  std::chrono::system_clock::time_point now = util::time::time_utility::now();
  if (now > next_update_timepoint_) {
    if (try_update()) {
      ++ret;
    }
  }

  return ret;
}

bool rank_settlement_manager::is_update_task_running() const {
  if (task_type_trait::empty(update_task_)) {
    return false;
  }

  if (task_type_trait::is_exiting(update_task_)) {
    task_type_trait::reset_task(update_task_);
    return false;
  }

  return true;
}

bool rank_settlement_manager::try_update() {
  if (is_exiting_) {
    return false;
  }

  auto now = util::time::time_utility::now();

  // 保护时间
  if (now <= next_update_timepoint_) {
    return false;
  }

  // 只允许一个任务进行
  if (is_update_task_running()) {
    return false;
  }

  auto timeout = task_manager::make_timeout_duration(logic_config::me()->get_cfg_task().nomsg().timeout());
  if (timeout <= std::chrono::seconds{60}) {
    timeout = task_manager::make_timeout_duration(std::chrono::seconds{600});
  }
  next_update_timepoint_ = now + timeout;

  task_type_trait::task_type task_inst;
  task_action_rank_update_settlement::ctor_param_t params;
  params.timeout = next_update_timepoint_;
  task_manager::me()->create_task_with_timeout<task_action_rank_update_settlement>(task_inst, timeout,
                                                                                   std::move(params));

  if (task_type_trait::empty(task_inst)) {
    FWLOGERROR("create task_action_rank_update_settlement failed");
  } else {
    update_task_ = task_inst;

    dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();

    int res = task_manager::me()->start_task(task_inst, start_data);
    if (res < 0) {
      FWLOGERROR("start task_action_rank_update_settlement failed, res: {}({})", res,
                 protobuf_mini_dumper_get_error_msg(res));
      task_type_trait::reset_task(update_task_);
      return false;
    }
  }

  return true;
}

void rank_settlement_manager::reset_update_protect(std::chrono::system_clock::time_point next_update_timepoint) {
  if (next_update_timepoint != next_update_timepoint_) {
    next_update_timepoint_ = next_update_timepoint;
  }
}