// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_manager.h"

#include <common/file_system.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/stubs/logging.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>

#include <string>

#include "dispatcher/task_action_base.h"

static void log_wrapper_for_protobuf(::google::protobuf::LogLevel level, const char *filename, int line,
                                     const std::string &message) {
  util::log::log_wrapper::caller_info_t caller;
  caller.file_path = filename;
  caller.line_number = static_cast<uint32_t>(line);
  caller.func_name = "protobuf";
  caller.rotate_index = 0;

  switch (level) {
    case ::google::protobuf::LOGLEVEL_INFO:
      caller.level_id = util::log::log_wrapper::level_t::LOG_LW_INFO;
      caller.level_name = "Info";
      break;

    case ::google::protobuf::LOGLEVEL_WARNING:
      caller.level_id = util::log::log_wrapper::level_t::LOG_LW_WARNING;
      caller.level_name = "Warn";
      break;

    case ::google::protobuf::LOGLEVEL_ERROR:
      caller.level_id = util::log::log_wrapper::level_t::LOG_LW_ERROR;
      caller.level_name = "Error";
      break;

    case ::google::protobuf::LOGLEVEL_FATAL:
      caller.level_id = util::log::log_wrapper::level_t::LOG_LW_FATAL;
      caller.level_name = "Fatal";
      break;

    default:
      caller.level_id = util::log::log_wrapper::level_t::LOG_LW_DEBUG;
      caller.level_name = "Debug";
      break;
  }

  if (util::log::log_wrapper::check_level(WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT),
                                          caller.level_id)) {
    WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->log(caller, "%s", message.c_str());
  }
}

task_manager::task_action_maker_base_t::task_action_maker_base_t(const atframework::DispatcherOptions *opt) {
  if (nullptr != opt) {
    options.CopyFrom(*opt);
  }
}

task_manager::task_action_maker_base_t::~task_action_maker_base_t() {}

task_manager::actor_action_maker_base_t::actor_action_maker_base_t(const atframework::DispatcherOptions *opt) {
  if (nullptr != opt) {
    options.CopyFrom(*opt);
  }
}

task_manager::actor_action_maker_base_t::~actor_action_maker_base_t() {}

task_manager::task_manager()
    : stat_interval_(60), stat_last_checkpoint_(0), conf_busy_count_(0), conf_busy_warn_count_(0) {}

task_manager::~task_manager() {
  // free protobuf meta
  ::google::protobuf::ShutdownProtobufLibrary();
}

int task_manager::init() {
  native_mgr_ = native_task_manager_type::create();
  stack_pool_ = task_types::stack_pool_type::create();

  // setup logger for protobuf
  ::google::protobuf::SetLogHandler(log_wrapper_for_protobuf);

  // reload is called before init when atapp started
  reload();

  if (!check_sys_config()) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  return 0;
}

int task_manager::reload() {
  stat_interval_ = logic_config::me()->get_cfg_task().nomsg().timeout().seconds();
  if (stat_interval_ <= 0) {
    stat_interval_ = 60;
  }
  if (stack_pool_) {
    stack_pool_->set_gc_once_number(logic_config::me()->get_cfg_task().stack().gc_once_number());
    stack_pool_->set_max_stack_number(logic_config::me()->get_cfg_task().stack().pool_max_count());
    stack_pool_->set_min_stack_number(logic_config::me()->get_cfg_task().stack().pool_min_count());
    if (logic_config::me()->get_cfg_task().stack().size() > 0) {
      stack_pool_->set_stack_size(logic_config::me()->get_cfg_task().stack().size());
    }
  }
  conf_busy_count_ = logic_config::me()->get_cfg_task().stack().busy_count();
  conf_busy_warn_count_ = logic_config::me()->get_cfg_task().stack().busy_warn_count();

  return 0;
}

int task_manager::start_task(id_t task_id, start_data_t &data) {
  int res = native_mgr_->start(task_id, &data);
  if (res < 0) {
    FWLOGERROR("start task {:#x} failed.", task_id);

    // 错误码
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  return 0;
}

int task_manager::resume_task(id_t task_id, resume_data_t &data) {
  int res = native_mgr_->resume(task_id, &data);
  if (res < 0) {
    if (copp::COPP_EC_NOT_FOUND == res) {
      FWLOGINFO("resume task {:#x} but not found, ignored.", task_id);
      return 0;
    }

    FWLOGERROR("resume task {:#x} failed, res: {}.", task_id, res);
    // 错误码
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  return 0;
}

int task_manager::tick(time_t sec, int nsec) {
  if (native_mgr_) {
    native_mgr_->tick(sec, nsec);
  }

  if (stack_pool_) {
    stack_pool_->gc();
  }

  if (stat_last_checkpoint_ != sec / stat_interval_) {
    stat_last_checkpoint_ = sec / stat_interval_;
    if (stack_pool_ && native_mgr_) {
      size_t first_checkpoint = 0;
      if (!native_mgr_->get_checkpoints().empty()) {
        first_checkpoint = native_mgr_->get_checkpoints().begin()->expired_time.tv_sec;
      }
      FWLOGWARNING(
          "[STATS] Coroutine stack stats:\n\tRuntime - Task Number: {}\n\tRuntime - Checkpoint Number: "
          "{}\n\tRuntime - Next Checkpoint: "
          "{}\n\tConfigure - Max GC Number: {}\n\tConfigure - Stack Max: number {}, size {}\n\tConfigure - "
          "Stack Min: number {}, size "
          "{}\n\tRuntime - Stack Used: number {}, size {}\n\tRuntime - Stack Free: number {}, size {}",
          native_mgr_->get_task_size(), native_mgr_->get_tick_checkpoint_size(), first_checkpoint,
          stack_pool_->get_gc_once_number(), stack_pool_->get_max_stack_number(), stack_pool_->get_max_stack_size(),
          stack_pool_->get_min_stack_number(), stack_pool_->get_min_stack_size(),
          stack_pool_->get_limit().used_stack_number, stack_pool_->get_limit().used_stack_size,
          stack_pool_->get_limit().free_stack_number, stack_pool_->get_limit().free_stack_size);
    }
  }
  return 0;
}

task_manager::task_ptr_t task_manager::get_task(id_t task_id) {
  if (!native_mgr_) {
    return task_manager::task_ptr_t();
  }

  if (stack_pool_) {
    stack_pool_->gc();
  }

  return native_mgr_->find_task(task_id);
}

size_t task_manager::get_stack_size() const { return logic_config::me()->get_cfg_task().stack().size(); }

int task_manager::add_task(const task_t::ptr_t &task, time_t timeout_sec, time_t timeout_nsec) {
  if (!native_mgr_) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  int res = 0;
  if (0 == timeout_sec && timeout_nsec == 0) {
    // read default timeout from configure
    res = native_mgr_->add_task(task, logic_config::me()->get_cfg_task().csmsg().timeout().seconds(), 0);
  } else {
    res = native_mgr_->add_task(task, timeout_sec, static_cast<int>(timeout_nsec));
  }

  if (res < 0) {
    FWLOGERROR("add task failed, res: {}", res);
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (conf_busy_warn_count_ > 0 && native_mgr_->get_task_size() > conf_busy_warn_count_) {
    FWLOGWARNING("task number {} extend {}", native_mgr_->get_task_size(), conf_busy_warn_count_);
  }

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_manager::report_create_error(const char *fn_name) {
  FWLOGERROR("[{}] create task failed. current task number={}", fn_name, native_mgr_->get_task_size());
  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SYS_MALLOC;
}

bool task_manager::is_busy() const { return conf_busy_count_ > 0 && native_mgr_->get_task_size() > conf_busy_count_; }

void task_manager::reset_private_data(task_private_data_t &priv_data) { priv_data.action = nullptr; }

task_manager::task_private_data_t *task_manager::get_private_data(task_t &task) {
  if (task.get_private_buffer_size() < sizeof(task_private_data_t)) {
    return nullptr;
  }

  return reinterpret_cast<task_private_data_t *>(task.get_private_buffer());
}

rpc::context *task_manager::get_shared_context(task_t &task) {
  task_private_data_t *task_priv_data = get_private_data(task);
  if (nullptr == task_priv_data) {
    return nullptr;
  }

  if (nullptr == task_priv_data->action) {
    return nullptr;
  }

  return &task_action_base::task_action_helper_t::get_shared_context(*task_priv_data->action);
}

bool task_manager::check_sys_config() const {
  const char *vm_map_count_file = "/proc/sys/vm/max_map_count";

  if (util::file_system::is_exist(vm_map_count_file)) {
    std::string content;
    util::file_system::get_file_content(content, vm_map_count_file);
    uint64_t sys_mmap_count = util::string::to_int<uint64_t>(content.c_str());
    if (logic_config::me()->get_cfg_task().stack().mmap_count() > sys_mmap_count) {
      FWLOGERROR("mmap_count {} is greater than /proc/sys/vm/max_map_count {}",
                 logic_config::me()->get_cfg_task().stack().mmap_count(), sys_mmap_count);

      return false;
    }

    // 每个协程栈有一个栈的段和一个protect段，占两个
    uint64_t task_max_num = logic_config::me()->get_cfg_task().stack().busy_count();
    if (task_max_num < logic_config::me()->get_cfg_task().stack().pool_max_count()) {
      task_max_num = logic_config::me()->get_cfg_task().stack().pool_max_count();
    }
    uint64_t check_mmap = 2 * task_max_num + logic_config::me()->get_cfg_task().stack().keep_count();
    if (check_mmap > logic_config::me()->get_cfg_task().stack().mmap_count()) {
      FWLOGERROR(
          "2 * max(busy_count, pool_max_count) + keep_count {} is greater than "
          "mmap_count {}",
          check_mmap, logic_config::me()->get_cfg_task().stack().mmap_count());

      return false;
    }
  }

  return true;
}