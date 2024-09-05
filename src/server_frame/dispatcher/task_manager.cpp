// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_manager.h"

#include <common/file_system.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#if GOOGLE_PROTOBUF_VERSION >= 4022000
#  include <absl/log/internal/log_sink_set.h>
#else
#  include <google/protobuf/stubs/logging.h>
#endif

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <grpc/grpc.h>

#include <atframe/atapp.h>

#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

#include <assert.h>
#include <atomic>
#include <string>

#include "dispatcher/task_action_base.h"
#include "rpc/telemetry/opentelemetry_utility.h"
#include "rpc/telemetry/rpc_global_service.h"

namespace {
struct task_manager_metrics_data_type {
  std::atomic<size_t> task_count;
  std::atomic<size_t> task_max_count;
  std::atomic<size_t> tick_checkpoint_count;
  std::atomic<size_t> pool_free_memory;
  std::atomic<size_t> pool_used_memory;
};

static task_manager_metrics_data_type &get_task_manager_metrics_data() {
  static task_manager_metrics_data_type ret;
  return ret;
}

#if GOOGLE_PROTOBUF_VERSION >= 4022000
class UTIL_SYMBOL_LOCAL absl_global_log_sink : public absl::LogSink {
  void Send(const absl::LogEntry &entry) override {
    util::log::log_wrapper::caller_info_t caller;
    auto source_filename = entry.source_filename();
    caller.file_path = gsl::string_view{source_filename.data(), source_filename.size()};
    caller.line_number = static_cast<uint32_t>(entry.source_line());
    caller.func_name = "protobuf";
    caller.rotate_index = 0;

    switch (entry.log_severity()) {
      case ::absl::LogSeverity::kInfo:
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_INFO;
        caller.level_name = "Info";
        break;

      case ::absl::LogSeverity::kWarning:
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_WARNING;
        caller.level_name = "Warn";
        break;

      case ::absl::LogSeverity::kError:
        caller.level_id = util::log::log_wrapper::level_t::LOG_LW_ERROR;
        caller.level_name = "Error";
        break;

      case ::absl::LogSeverity::kFatal:
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
      auto text_message = entry.text_message();
      WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)
          ->format_log(caller, "{}", gsl::string_view{text_message.data(), text_message.size()});
    }
  }

  void Flush() override {}
};
#else
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
#endif

}  // namespace

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DATA_DEFINITION(task_manager);
#  else
UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DATA_DEFINITION(task_manager);
#  endif
#else
UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DATA_DEFINITION(task_manager);
#endif

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
SERVER_FRAME_API task_manager::start_error_transform::start_error_transform() {}

SERVER_FRAME_API task_manager::start_error_transform::~start_error_transform() {}

SERVER_FRAME_API task_manager::start_error_transform::start_error_transform(const start_error_transform &) {}

SERVER_FRAME_API task_manager::start_error_transform::start_error_transform(start_error_transform &&) {}

SERVER_FRAME_API task_manager::start_error_transform &task_manager::start_error_transform::operator=(
    const start_error_transform &) {
  return *this;
}

SERVER_FRAME_API task_manager::start_error_transform &task_manager::start_error_transform::operator=(
    start_error_transform &&) {
  return *this;
}

SERVER_FRAME_API std::pair<int32_t, dispatcher_start_data_type *> task_manager::start_error_transform::operator()(
    copp::promise_status in) const noexcept {
  switch (in) {
    case copp::promise_status::kInvalid:
    case copp::promise_status::kCreated:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_INIT, nullptr};
    case copp::promise_status::kRunning:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY, nullptr};
    case copp::promise_status::kDone:
      return {0, nullptr};
    case copp::promise_status::kCancle:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED, nullptr};
    case copp::promise_status::kKilled:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED, nullptr};
    case copp::promise_status::kTimeout:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, nullptr};
    default:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN, nullptr};
  }
}

SERVER_FRAME_API task_manager::resume_error_transform::resume_error_transform() {}

SERVER_FRAME_API task_manager::resume_error_transform::~resume_error_transform() {}

SERVER_FRAME_API task_manager::resume_error_transform::resume_error_transform(const resume_error_transform &) {}

SERVER_FRAME_API task_manager::resume_error_transform::resume_error_transform(resume_error_transform &&) {}

SERVER_FRAME_API task_manager::resume_error_transform &task_manager::resume_error_transform::operator=(
    const resume_error_transform &) {
  return *this;
}

SERVER_FRAME_API task_manager::resume_error_transform &task_manager::resume_error_transform::operator=(
    resume_error_transform &&) {
  return *this;
}

SERVER_FRAME_API std::pair<int32_t, dispatcher_resume_data_type *> task_manager::resume_error_transform::operator()(
    copp::promise_status in) const noexcept {
  switch (in) {
    case copp::promise_status::kInvalid:
    case copp::promise_status::kCreated:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_INIT, nullptr};
    case copp::promise_status::kRunning:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY, nullptr};
    case copp::promise_status::kDone:
      return {0, nullptr};
    case copp::promise_status::kCancle:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED, nullptr};
    case copp::promise_status::kKilled:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED, nullptr};
    case copp::promise_status::kTimeout:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT, nullptr};
    default:
      return {PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN, nullptr};
  }
}

SERVER_FRAME_API task_manager::generic_resume_key::generic_resume_key() {}

SERVER_FRAME_API task_manager::generic_resume_key::~generic_resume_key() {}

SERVER_FRAME_API task_manager::generic_resume_key::generic_resume_key(const generic_resume_key &other)
    : timeout(other.timeout), message_type(other.message_type), sequence(other.sequence) {}

SERVER_FRAME_API task_manager::generic_resume_key::generic_resume_key(generic_resume_key &&other)
    : timeout(other.timeout), message_type(other.message_type), sequence(other.sequence) {}

SERVER_FRAME_API task_manager::generic_resume_key &task_manager::generic_resume_key::operator=(
    const generic_resume_key &other) {
  timeout = other.timeout;
  message_type = other.message_type;
  sequence = other.sequence;
  return *this;
}

SERVER_FRAME_API task_manager::generic_resume_key &task_manager::generic_resume_key::operator=(
    generic_resume_key &&other) {
  timeout = other.timeout;
  message_type = other.message_type;
  sequence = other.sequence;
  return *this;
}

SERVER_FRAME_API task_manager::generic_resume_index::generic_resume_index() {}

SERVER_FRAME_API task_manager::generic_resume_index::~generic_resume_index() {}

SERVER_FRAME_API task_manager::generic_resume_index::generic_resume_index(const generic_resume_index &other)
    : message_type(other.message_type), sequence(other.sequence) {}

SERVER_FRAME_API task_manager::generic_resume_index::generic_resume_index(generic_resume_index &&other)
    : message_type(other.message_type), sequence(other.sequence) {}

SERVER_FRAME_API task_manager::generic_resume_index &task_manager::generic_resume_index::operator=(
    const generic_resume_index &other) {
  message_type = other.message_type;
  sequence = other.sequence;
  return *this;
}

SERVER_FRAME_API task_manager::generic_resume_index &task_manager::generic_resume_index::operator=(
    generic_resume_index &&other) {
  message_type = other.message_type;
  sequence = other.sequence;
  return *this;
}

SERVER_FRAME_API task_manager::generic_resume_hash::generic_resume_hash() {}

SERVER_FRAME_API task_manager::generic_resume_hash::~generic_resume_hash() {}

SERVER_FRAME_API task_manager::generic_resume_hash::generic_resume_hash(const generic_resume_hash &) {}

SERVER_FRAME_API task_manager::generic_resume_hash::generic_resume_hash(generic_resume_hash &&) {}

SERVER_FRAME_API task_manager::generic_resume_hash &task_manager::generic_resume_hash::operator=(
    const generic_resume_hash &) {
  return *this;
}

SERVER_FRAME_API task_manager::generic_resume_hash &task_manager::generic_resume_hash::operator=(
    generic_resume_hash &&) {
  return *this;
}

#endif

SERVER_FRAME_API task_manager::task_action_maker_base_t::task_action_maker_base_t(
    const atframework::DispatcherOptions *opt) {
  if (nullptr != opt) {
    options.CopyFrom(*opt);
  }
}

SERVER_FRAME_API task_manager::task_action_maker_base_t::~task_action_maker_base_t() {}

SERVER_FRAME_API task_manager::task_manager()
    : stat_interval_(60), stat_last_checkpoint_(0), conf_busy_count_(0), conf_busy_warn_count_(0) {
  get_task_manager_metrics_data().task_count.store(0, std::memory_order_release);
  get_task_manager_metrics_data().task_max_count.store(0, std::memory_order_release);
  get_task_manager_metrics_data().tick_checkpoint_count.store(0, std::memory_order_release);
  get_task_manager_metrics_data().pool_free_memory.store(0, std::memory_order_release);
  get_task_manager_metrics_data().pool_used_memory.store(0, std::memory_order_release);

  // task_manager 必须在全局变量之后构造。并且进入析构阶段不允许再创建
  assert(nullptr != atapp::app::get_last_instance());
}

SERVER_FRAME_API task_manager::~task_manager() {
  if (native_mgr_) {
    native_mgr_->reset();
  }
  native_mgr_.reset();
#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
  stack_pool_.reset();
#endif

  // Free protobuf meta
  ::google::protobuf::ShutdownProtobufLibrary();

#if GOOGLE_PROTOBUF_VERSION >= 4022000
  if (absl_log_sink_) {
    absl::log_internal::RemoveLogSink(absl_log_sink_.get());
    absl_log_sink_.reset();
  }
#endif
}

SERVER_FRAME_API int task_manager::init() {
  native_mgr_ = native_task_manager_type::create();
#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
  stack_pool_ = task_type_trait::stack_pool_type::create();
#endif

// setup logger for protobuf
#if GOOGLE_PROTOBUF_VERSION >= 4022000
  if (absl_log_sink_) {
    absl::log_internal::RemoveLogSink(absl_log_sink_.get());
    absl_log_sink_.reset();
  }
  absl_log_sink_.reset(new absl_global_log_sink());
  absl::log_internal::AddLogSink(absl_log_sink_.get());
#else
  ::google::protobuf::SetLogHandler(log_wrapper_for_protobuf);
#endif

  // reload is called before init when atapp started
  reload();

  if (!check_sys_config()) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  atapp::app *app = atapp::app::get_last_instance();
  if (nullptr != app) {
    // Cleanup and destroy task manager
    native_task_manager_ptr_type hold_task_manager = native_mgr_;
    app->add_evt_on_finally([hold_task_manager](atapp::app &) {
      if (hold_task_manager) {
        hold_task_manager->reset();
      }

      if (!task_manager::is_instance_destroyed()) {
        task_manager::me()->native_mgr_.reset();
      }
    });
  }

  setup_metrics();
  return 0;
}

SERVER_FRAME_API int task_manager::reload() {
  stat_interval_ = logic_config::me()->get_cfg_task().nomsg().timeout().seconds();
  if (stat_interval_ <= 0) {
    stat_interval_ = 60;
  }
#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
  if (stack_pool_) {
    stack_pool_->set_gc_once_number(logic_config::me()->get_cfg_task().stack().gc_once_number());
    stack_pool_->set_max_stack_number(logic_config::me()->get_cfg_task().stack().pool_max_count());
    stack_pool_->set_min_stack_number(logic_config::me()->get_cfg_task().stack().pool_min_count());
    if (logic_config::me()->get_cfg_task().stack().size() > 0) {
      stack_pool_->set_stack_size(logic_config::me()->get_cfg_task().stack().size());
    }
  }
#endif
  conf_busy_count_ = logic_config::me()->get_cfg_task().stack().busy_count();
  conf_busy_warn_count_ = logic_config::me()->get_cfg_task().stack().busy_warn_count();

  return 0;
}

SERVER_FRAME_API int task_manager::start_task(task_type_trait::id_type task_id, dispatcher_start_data_type &data) {
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  auto iter = waiting_start_.find(task_id);
  if (iter == waiting_start_.end()) {
    FWLOGERROR("start task {} failed.", task_id);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  auto generator = iter->second.generator_context;

  internal_trigger_callback(iter->second, &data);
  if (generator) {
    generator->set_value(std::pair<int32_t, dispatcher_start_data_type *>{0, &data});
  } else {
    waiting_start_.erase(iter);
  }
#else
  int res = native_mgr_->start(task_id, &data);
  if (res < 0) {
    FWLOGERROR("start task {} failed.", task_id);

    // 错误码
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
#endif
  return 0;
}

SERVER_FRAME_API int task_manager::resume_task(task_type_trait::id_type task_id, dispatcher_resume_data_type &data) {
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  generic_resume_index index{data.message.message_type, data.sequence};
  auto iter_index = waiting_resume_index_.find(index);
  if (iter_index == waiting_resume_index_.end()) {
    FWLOGINFO("resume task {}(message type={}, sequence={}) but generator index not found, ignored.", task_id,
              data.message.message_type, data.sequence);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  generic_resume_key key = iter_index->second;
  auto iter_key = waiting_resume_timer_.find(key);
  if (iter_key == waiting_resume_timer_.end()) {
    FWLOGERROR("resume task {}(message type={}, sequence={}) but generator timer not found, abort.", task_id,
               data.message.message_type, data.sequence);
    waiting_resume_index_.erase(iter_index);
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  auto generator = iter_key->second.generator_context;

  internal_trigger_callback(iter_key->second, key, &data);
  if (generator) {
    generator->set_value(std::pair<int32_t, dispatcher_resume_data_type *>{0, &data});
  } else {
    waiting_resume_index_.erase(iter_index);
    waiting_resume_timer_.erase(iter_key);
  }
#else
  int res = native_mgr_->resume(task_id, &data);
  if (res < 0) {
    if (copp::COPP_EC_NOT_FOUND == res) {
      FWLOGINFO("resume task {}(message type={}, sequence={}) but task not found, ignored.", task_id,
                data.message.message_type, data.sequence);
      return 0;
    }

    FWLOGERROR("resume task {}(message type={}, sequence={}) failed, res: {}.", task_id, data.message.message_type,
               data.sequence, res);
    // 错误码
    return PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
#endif

  return 0;
}

SERVER_FRAME_API int task_manager::tick(time_t sec, int nsec) {
  if (native_mgr_) {
    native_mgr_->tick(sec, nsec);
  }

#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
  if (stack_pool_) {
    stack_pool_->gc();
  }
#endif

  if (stat_last_checkpoint_ != sec / stat_interval_) {
    stat_last_checkpoint_ = sec / stat_interval_;
    if (native_mgr_) {
      time_t first_checkpoint = 0;
      if (!native_mgr_->get_checkpoints().empty()) {
        first_checkpoint = native_mgr_->get_checkpoints().begin()->expired_time.tv_sec;
      }

      get_task_manager_metrics_data().task_count.store(native_mgr_->get_task_size(), std::memory_order_release);
      get_task_manager_metrics_data().tick_checkpoint_count.store(native_mgr_->get_tick_checkpoint_size(),
                                                                  std::memory_order_release);

      FWLOGWARNING(
          "[STATISTICS] Coroutine task stats:\n\tRuntime - Task Number: {}\n\tRuntime - Checkpoint Number: "
          "{}\n\tRuntime - Next Checkpoint: "
          "{}",
          native_mgr_->get_task_size(), native_mgr_->get_tick_checkpoint_size(), first_checkpoint);
    }
#if !(defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE)
    if (stack_pool_) {
      get_task_manager_metrics_data().pool_free_memory.store(stack_pool_->get_limit().free_stack_size,
                                                             std::memory_order_release);
      get_task_manager_metrics_data().pool_used_memory.store(stack_pool_->get_limit().used_stack_size,
                                                             std::memory_order_release);
      FWLOGWARNING(
          "[STATISTICS] Coroutine stack stats:\n\tConfigure - Max GC Number: {}\n\tConfigure - Stack Max: number {}, "
          "size "
          "{}\n\tConfigure - "
          "Stack Min: number {}, size "
          "{}\n\tRuntime - Stack Used: number {}, size {}\n\tRuntime - Stack Free: number {}, size {}",
          stack_pool_->get_gc_once_number(), stack_pool_->get_max_stack_number(), stack_pool_->get_max_stack_size(),
          stack_pool_->get_min_stack_number(), stack_pool_->get_min_stack_size(),
          stack_pool_->get_limit().used_stack_number, stack_pool_->get_limit().used_stack_size,
          stack_pool_->get_limit().free_stack_number, stack_pool_->get_limit().free_stack_size);
    }
#endif
  }
  return 0;
}

SERVER_FRAME_API void task_manager::kill_all() {
  if (!native_mgr_) {
    return;
  }

  while (native_mgr_->get_task_size() > 0) {
    native_mgr_->reset();
  }
}

SERVER_FRAME_API task_type_trait::task_type task_manager::get_task(task_type_trait::id_type task_id) {
  if (!native_mgr_) {
    return task_type_trait::task_type();
  }

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  auto task_ptr = native_mgr_->find_task(task_id);
  if (nullptr == task_ptr) {
    return task_type_trait::task_type();
  }
  return *task_ptr;
#else
  if (stack_pool_) {
    stack_pool_->gc();
  }

  return native_mgr_->find_task(task_id);
#endif
}

SERVER_FRAME_API std::chrono::system_clock::duration task_manager::get_default_timeout() {
  auto ret = make_timeout_duration(logic_config::me()->get_cfg_task().csmsg().timeout());
  if (ret <= ret.zero()) {
    ret = make_timeout_duration(std::chrono::seconds{8});
  }
  return ret;
}

SERVER_FRAME_API size_t task_manager::get_stack_size() const {
  return logic_config::me()->get_cfg_task().stack().size();
}

int task_manager::add_task(const task_type_trait::task_type &task, std::chrono::system_clock::duration timeout) {
  if (!native_mgr_) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
  }

  int res = 0;
  if (timeout <= std::chrono::system_clock::duration::zero()) {
    // read default timeout from configure
    res = native_mgr_->add_task(task, logic_config::me()->get_cfg_task().csmsg().timeout().seconds(),
                                logic_config::me()->get_cfg_task().csmsg().timeout().nanos());
  } else {
    time_t timeout_sec = static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
    int timeout_nsec = static_cast<int>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - std::chrono::seconds{timeout_sec}).count());
    res = native_mgr_->add_task(task, timeout_sec, timeout_nsec);
  }

  if (res < 0) {
    FWLOGERROR("add task failed, result: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
  }

  if (conf_busy_warn_count_ > 0 && native_mgr_->get_task_size() > conf_busy_warn_count_) {
    // TODO(owentou): 监控告警
    FWLOGWARNING("task number {} extend {}", native_mgr_->get_task_size(), conf_busy_warn_count_);
  }

  if (native_mgr_->get_task_size() > get_task_manager_metrics_data().task_max_count.load(std::memory_order_acquire)) {
    get_task_manager_metrics_data().task_max_count.store(native_mgr_->get_task_size(), std::memory_order_release);
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_manager::report_create_error(const char *fn_name) {
  FWLOGERROR("[{}] create task failed. current task number={}", fn_name, native_mgr_->get_task_size());
  return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
}

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
void task_manager::internal_insert_start_generator(task_type_trait::id_type task_id,
                                                   generic_start_generator::context_pointer_type &&generator_context,
                                                   dispatcher_receive_start_data_callback callback,
                                                   void *callback_private_data) {
  if (!generator_context) {
    return;
  }

  generic_start_generator_record record;
  record.generator_context = std::move(generator_context);
  record.callback = callback;
  record.callback_private_data = callback_private_data;

  waiting_start_.emplace(
      std::pair<const task_type_trait::id_type, generic_start_generator_record>{task_id, std::move(record)});
}

void task_manager::internal_remove_start_generator(task_type_trait::id_type task_id,
                                                   const generic_start_generator::context_type &generator_context) {
  auto iter = waiting_start_.find(task_id);
  if (iter == waiting_start_.end()) {
    return;
  }

  if (nullptr != generator_context.data()) {
    internal_trigger_callback(iter->second, generator_context.data()->second);
  }

  waiting_start_.erase(iter);
}

void task_manager::internal_insert_resume_generator(const generic_resume_key &key,
                                                    generic_resume_generator::context_pointer_type &&generator_context,
                                                    dispatcher_receive_resume_data_callback callback,
                                                    void *callback_private_data) {
  if (!generator_context) {
    return;
  }

  generic_resume_index index{key.message_type, key.sequence};
  if (waiting_resume_index_.end() != waiting_resume_index_.find(index)) {
    return;
  }

  generic_resume_generator_record record;
  record.generator_context = std::move(generator_context);
  record.callback = callback;
  record.callback_private_data = callback_private_data;

  waiting_resume_timer_.emplace(
      std::pair<const generic_resume_key, generic_resume_generator_record>{key, std::move(record)});
  waiting_resume_index_.emplace(std::pair<const generic_resume_index, generic_resume_key>{index, key});
}

void task_manager::internal_remove_resume_generator(const generic_resume_key &key,
                                                    const generic_resume_generator::context_type &generator_context) {
  auto iter = waiting_resume_timer_.find(key);
  if (iter == waiting_resume_timer_.end()) {
    return;
  }

  if (iter->second.generator_context && iter->second.generator_context.get() != &generator_context) {
    return;
  }

  if (nullptr != generator_context.data()) {
    internal_trigger_callback(iter->second, iter->first, generator_context.data()->second);
  }

  waiting_resume_index_.erase({generic_resume_index{key.message_type, key.sequence}});
  waiting_resume_timer_.erase(iter);
}

void task_manager::internal_trigger_callback(generic_start_generator_record &start_record,
                                             const dispatcher_start_data_type *start_data) {
  if (!start_record.callback) {
    return;
  }

  dispatcher_receive_start_data_callback callback = start_record.callback;
  void *callback_private_data = start_record.callback_private_data;
  start_record.callback = nullptr;
  start_record.callback_private_data = nullptr;

  if (nullptr == start_data) {
    (*callback)(start_data, callback_private_data);
    return;
  }

  (*callback)(start_data, callback_private_data);
}

void task_manager::internal_trigger_callback(generic_resume_generator_record &resume_record,
                                             const generic_resume_key &key,
                                             const dispatcher_resume_data_type *resume_data) {
  if (!resume_record.callback) {
    return;
  }

  dispatcher_receive_resume_data_callback callback = resume_record.callback;
  void *callback_private_data = resume_record.callback_private_data;
  resume_record.callback = nullptr;
  resume_record.callback_private_data = nullptr;

  if (nullptr == resume_data) {
    (*callback)(resume_data, callback_private_data);
    return;
  }

  if (resume_data->message.message_type != key.message_type) {
    FWLOGINFO("resume and expect message type {:#x} but real is {:#x}, ignore this message", key.message_type,
              resume_data->message.message_type);
    return;
  }
  if (0 != resume_data->sequence && 0 != key.sequence && resume_data->sequence != key.sequence) {
    FWLOGINFO("resume and expect message sequence {:#x} but real is {:#x}, ignore this message", key.sequence,
              resume_data->sequence);
    return;
  }
  (*callback)(resume_data, callback_private_data);
}

#endif

SERVER_FRAME_API bool task_manager::is_busy() const {
  return conf_busy_count_ > 0 && native_mgr_->get_task_size() > conf_busy_count_;
}

SERVER_FRAME_API void task_manager::reset_private_data(task_private_data_type &priv_data) {
  priv_data.action = nullptr;
}

SERVER_FRAME_API rpc::context *task_manager::get_shared_context(task_type_trait::task_type &task) {
  task_private_data_type *task_priv_data = task_type_trait::get_private_data(task);
  if (nullptr == task_priv_data) {
    return nullptr;
  }

  if (nullptr == task_priv_data->action) {
    return nullptr;
  }

  return &task_action_base::task_action_helper_t::get_shared_context(*task_priv_data->action);
}

SERVER_FRAME_API int32_t
task_manager::convert_task_status_to_error_code(task_type_trait::task_status task_status) noexcept {
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  switch (task_status) {
    case task_type_trait::task_status::kCancle: {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
    }
    case task_type_trait::task_status::kKilled: {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
    }
    case task_type_trait::task_status::kTimeout: {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    }
    default: {
      break;
    }
  }

  if (task_status >= task_type_trait::task_status::kDone) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
#else
  switch (task_status) {
    case cotask::EN_TS_CANCELED: {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
    }
    case cotask::EN_TS_KILLED: {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
    }
    case cotask::EN_TS_TIMEOUT: {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    }
    default: {
      break;
    }
  }

  if (task_status >= cotask::EN_TS_DONE) {
    return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING;
  }

  return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
#endif
}

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
SERVER_FRAME_API task_manager::generic_start_generator task_manager::make_start_generator(
    task_type_trait::id_type task_id, dispatcher_receive_start_data_callback receive_callback,
    void *callback_private_data) {
  return {[task_id, receive_callback, callback_private_data](generic_start_generator::context_pointer_type generator) {
            if (task_manager::is_instance_destroyed()) {
              return;
            }

            task_manager::me()->internal_insert_start_generator(task_id, std::move(generator), receive_callback,
                                                                callback_private_data);
          },
          [task_id](const generic_start_generator::context_type &generator) {
            if (task_manager::is_instance_destroyed()) {
              return;
            }

            task_manager::me()->internal_remove_start_generator(task_id, generator);
          }};
}

SERVER_FRAME_API std::pair<task_manager::generic_resume_key, task_manager::generic_resume_generator>
task_manager::make_resume_generator(uintptr_t message_type, const dispatcher_await_options &await_options,
                                    dispatcher_receive_resume_data_callback receive_callback,
                                    void *callback_private_data) {
  std::chrono::system_clock::duration timeout = await_options.timeout;
  if (timeout <= std::chrono::system_clock::duration::zero()) {
    timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::seconds{logic_config::me()->get_cfg_task().csmsg().timeout().seconds()} +
        std::chrono::nanoseconds{logic_config::me()->get_cfg_task().csmsg().timeout().nanos()});
  }

  generic_resume_key key{util::time::time_utility::now() + timeout, message_type, await_options.sequence};
  return {key,
          {[key, receive_callback, callback_private_data](generic_resume_generator::context_pointer_type generator) {
             if (task_manager::is_instance_destroyed()) {
               return;
             }

             task_manager::me()->internal_insert_resume_generator(key, std::move(generator), receive_callback,
                                                                  callback_private_data);
           },
           [key](const generic_resume_generator::context_type &generator) {
             if (task_manager::is_instance_destroyed()) {
               return;
             }

             task_manager::me()->internal_remove_resume_generator(key, generator);
           }}};
}
#endif

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

void task_manager::setup_metrics() {
  rpc::telemetry::global_service::add_on_ready([]() {
    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "service_coroutine", {"service_coroutine_task_count", "", ""},
        [](opentelemetry::metrics::ObserverResult &result) {
          rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
              result, static_cast<int64_t>(get_task_manager_metrics_data().task_count.load(std::memory_order_acquire)));
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "service_coroutine",
        {"service_coroutine_task_max_count", "", ""}, [](opentelemetry::metrics::ObserverResult &result) {
          auto &metrics_data = get_task_manager_metrics_data();
          int64_t ret = static_cast<int64_t>(metrics_data.task_max_count.load(std::memory_order_acquire));
          metrics_data.task_max_count.store(metrics_data.task_count.load(std::memory_order_acquire),
                                            std::memory_order_release);
          rpc::telemetry::opentelemetry_utility::global_metics_observe_record(result, ret);
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "service_coroutine",
        {"service_coroutine_tick_checkpoint_count", "", ""}, [](opentelemetry::metrics::ObserverResult &result) {
          rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
              result, static_cast<int64_t>(
                          get_task_manager_metrics_data().tick_checkpoint_count.load(std::memory_order_acquire)));
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "service_coroutine",
        {"service_coroutine_pool_free_memory", "", ""}, [](opentelemetry::metrics::ObserverResult &result) {
          rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
              result,
              static_cast<int64_t>(get_task_manager_metrics_data().pool_free_memory.load(std::memory_order_acquire)));
        });

    rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
        rpc::telemetry::metrics_observable_type::kGauge, "service_coroutine",
        {"service_coroutine_pool_used_memory", "", ""}, [](opentelemetry::metrics::ObserverResult &result) {
          rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
              result,
              static_cast<int64_t>(get_task_manager_metrics_data().pool_used_memory.load(std::memory_order_acquire)));
        });
  });
}
