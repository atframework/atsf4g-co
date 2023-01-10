// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_action_base.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/extern_log_categorize.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/db/uuid.h>

#include <config/logic_config.h>

#include <functional>
#include <utility>

#include "dispatcher/task_manager.h"

namespace detail {
struct task_action_stat_guard {
  explicit task_action_stat_guard(task_action_base *act) : action(act) {
    if (nullptr != action) {
      util::time::time_utility::update();
      start = util::time::time_utility::now();
    }
  }

  ~task_action_stat_guard() {
    if (nullptr == action) {
      return;
    }

    util::time::time_utility::update();
    util::time::time_utility::raw_time_t end = util::time::time_utility::now();
    if (logic_config::me()->get_cfg_task().stats().enable_internal_pstat_log()) {
      if (0 != action->get_user_id()) {
        FWCLOGINFO(log_categorize_t::PROTO_STAT, "{}|{}|{}us|{}|{}", action->name(), action->get_user_id(),
                   std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(), action->get_result(),
                   action->get_response_code());
      } else {
        FWCLOGINFO(log_categorize_t::PROTO_STAT, "{}|NO PLAYER|{}us|{}|{}", action->name(),
                   std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(), action->get_result(),
                   action->get_response_code());
      }
    }
  }

  util::time::time_utility::raw_time_t start;
  task_action_base *action;
};
}  // namespace detail

rpc::context &task_action_base::task_action_helper_t::get_shared_context(task_action_base &action) {
  return action.get_shared_context();
}

task_action_base::task_action_base()
    : user_id_(0),
      zone_id_(0),
      task_id_(0),
      private_data_(nullptr),
      result_(0),
      response_code_(0),
      response_message_disabled_(false),
      event_disabled_(false),
      start_data_(dispatcher_make_default<dispatcher_start_data_t>()) {}

task_action_base::task_action_base(rpc::context *caller_context)
    : user_id_(0),
      zone_id_(0),
      task_id_(0),
      private_data_(nullptr),
      result_(0),
      response_code_(0),
      response_message_disabled_(false),
      event_disabled_(false),
      start_data_(dispatcher_make_default<dispatcher_start_data_t>()) {
  if (nullptr != caller_context) {
    set_caller_context(*caller_context);
  }
}

task_action_base::~task_action_base() {}

const char *task_action_base::name() const {
  const char *ret = typeid(*this).name();
  if (nullptr == ret) {
    return "RTTI Unavailable: task_action_base";
  }

  // some compiler will generate number to mark the type
  while (ret && *ret >= '0' && *ret <= '9') {
    ++ret;
  }
  return ret;
}

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
int task_action_base::operator()(task_meta_data_type &&task_meta, void *priv_data) {
#else
int task_action_base::operator()(void *priv_data) {
#endif
  detail::task_action_stat_guard stat(this);

  rpc::context::trace_option trace_option;
  trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
  trace_option.is_remote = true;
  trace_option.dispatcher = get_dispatcher();
  trace_option.parent_network_span = nullptr;

  if (nullptr != priv_data) {
    start_data_ = *reinterpret_cast<dispatcher_start_data_t *>(priv_data);

    // Set parent context if not set by child type
    if (nullptr != start_data_.context) {
      set_caller_context(*start_data_.context);
    }
  }

  rpc::context::tracer tracer;
  shared_context_.setup_tracer(tracer, name(), std::move(trace_option));

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  private_data_ = task_meta.private_data;
  task_id_ = task_meta.task_id;
#else
  task_type_trait::internal_task_type *task = cotask::this_task::get<task_type_trait::internal_task_type>();
  if (nullptr == task) {
    FWLOGERROR("task convert failed, must in task.");
    return tracer.return_code(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
  }
  private_data_ = task_manager::get_private_data(*task);
  task_id_ = task->get_id();
#endif

  if (nullptr != private_data_) {
    // setup action
    private_data_->action = this;
  }

  {
    rpc::context::task_context_data rpc_task_context_data;
    rpc_task_context_data.task_id = task_id_;
    shared_context_.set_task_context(rpc_task_context_data);
  }

  if (0 != get_user_id()) {
    FWLOGDEBUG("task {} [{}] for player {}:{} start to run\n", name(), get_task_id(), get_zone_id(), get_user_id());
  } else {
    FWLOGDEBUG("task {} [{}] start to run\n", name(), get_task_id());
  }

  result_ = hook_run();

  if (event_disabled_) {
    if (result_ < 0) {
      FWLOGERROR("task {} [{}] without evt ret code ({}){} rsp code ({}){}\n", name(), get_task_id(),
                 protobuf_mini_dumper_get_error_msg(result_), result_,
                 protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
    } else {
      FWLOGDEBUG("task {} [{}] without evt ret code ({}){}, rsp code ({}){}\n", name(), get_task_id(),
                 protobuf_mini_dumper_get_error_msg(result_), result_,
                 protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
    }

    if (!response_message_disabled_) {
      send_response();
    }

    _notify_finished(*task);
    return tracer.return_code(result_);
  }
  // 响应OnSuccess(这时候任务的status还是running)
  if (cotask::EN_TS_RUNNING == task->get_status() && result_ >= 0) {
    int ret = 0;
    if (response_code_ < 0) {
      ret = on_failed();
      FWLOGINFO("task {} [{}] finished success but response errorcode, rsp code: ({}){}\n", name(), get_task_id(),
                protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
    } else {
      ret = on_success();
    }

    int complete_res = on_complete();
    if (0 != complete_res) {
      ret = complete_res;
    }

    if (!response_message_disabled_) {
      send_response();
    }

    _notify_finished(*task);
    return tracer.return_code(ret);
  }

  if (PROJECT_NAMESPACE_ID::err::EN_SUCCESS == result_) {
    if (task->is_timeout()) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    } else if (task->is_faulted()) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
    } else if (task->is_canceled()) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
    } else if (task->is_exiting()) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING;
    } else {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
    }
  }

  if (PROJECT_NAMESPACE_ID::EN_SUCCESS == response_code_) {
    if (task->is_timeout()) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT;
    } else if (task->is_faulted()) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    } else if (task->is_canceled()) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    } else if (task->is_exiting()) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    } else {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN;
    }
  }

  if (0 != get_user_id()) {
    FWLOGERROR("task {} [{}] for player {}:{} ret code ({}){}, rsp code ({}){}\n", name(), get_task_id(), get_zone_id(),
               get_user_id(), protobuf_mini_dumper_get_error_msg(result_), result_,
               protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
  } else {
    FWLOGERROR("task {} [{}] ret code ({}){}, rsp code ({}){}\n", name(), get_task_id(),
               protobuf_mini_dumper_get_error_msg(result_), result_, protobuf_mini_dumper_get_error_msg(response_code_),
               response_code_);
  }

  // 响应OnTimeout
  if (cotask::EN_TS_TIMEOUT == task->get_status()) {
    on_timeout();
  }

  // 如果不是running且不是timeout，可能是其他原因被kill掉了，响应OnFailed
  int ret = on_failed();

  int complete_res = on_complete();
  if (0 != complete_res) {
    ret = complete_res;
  }

  if (!response_message_disabled_) {
    send_response();
  }

  _notify_finished(*task);

  if (result_ >= 0) {
    ret = result_;
  }
  return tracer.return_code(ret);
}

task_action_base::result_type task_action_base::hook_run() { return (*this)(); }

int task_action_base::on_success() { return get_result(); }

int task_action_base::on_failed() { return get_result(); }

int task_action_base::on_timeout() { return 0; }

int task_action_base::on_complete() { return 0; }

rpc::context::parent_mode task_action_base::get_caller_mode() const noexcept {
  return rpc::context::parent_mode::kLink;
}

uint64_t task_action_base::get_task_id() const { return task_id_; }

task_action_base::on_finished_callback_handle_t task_action_base::add_on_on_finished(on_finished_callback_fn_t &&fn) {
  return on_finished_callback_.insert(on_finished_callback_.end(), std::move(fn));
}

void task_action_base::remove_on_finished(on_finished_callback_handle_t handle) { on_finished_callback_.erase(handle); }

void task_action_base::set_caller_context(rpc::context &ctx) {
  get_shared_context().set_parent_context(ctx, rpc::context::inherit_options{get_caller_mode()});
}

void task_action_base::_notify_finished(cotask::impl::task_impl &task_inst) {
  // Additional trace data
  auto trace_span = shared_context_.get_trace_span();
  if (trace_span) {
    if (0 != get_user_id() && 0 != get_zone_id()) {
      trace_span->SetAttribute("user_id", get_user_id());
      trace_span->SetAttribute("zone_id", get_zone_id());
    }
    trace_span->SetAttribute("response_code", get_response_code());
  }

  // Callbacks
  for (on_finished_callback_fn_t &fn : on_finished_callback_) {
    if (fn) {
      fn(*this);
    }
  }
  on_finished_callback_.clear();

  if (nullptr != private_data_) {
    // setup action
    private_data_->action = nullptr;
  }
}
