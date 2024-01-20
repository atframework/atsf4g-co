// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_action_base.h"

//clang-format off
#include <config/compiler/protobuf_prefix.h>
//clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

//clang-format off
#include <config/compiler/protobuf_suffix.h>
//clang-format on

#include <opentelemetry/trace/semantic_conventions.h>

#include <config/extern_log_categorize.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/db/uuid.h>
#include <rpc/telemetry/semantic_conventions.h>

#include <config/logic_config.h>

#include <functional>
#include <utility>

#include "dispatcher/dispatcher_implement.h"
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
        FWCLOGINFO(log_categorize_t::PROTO_STAT, "{}|{}|{}|{}us|{}|{}", action->name(), action->get_task_id(),
                   action->get_user_id(), std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(),
                   action->get_result(), action->get_response_code());
      } else {
        FWCLOGINFO(log_categorize_t::PROTO_STAT, "{}|{}|NO PLAYER|{}us|{}|{}", action->name(), action->get_task_id(),
                   std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(), action->get_result(),
                   action->get_response_code());
      }
    }
  }

  util::time::time_utility::raw_time_t start;
  task_action_base *action;
};
}  // namespace detail

SERVER_FRAME_API rpc::context &task_action_base::task_action_helper_t::get_shared_context(task_action_base &action) {
  return action.get_shared_context();
}

SERVER_FRAME_API task_action_base::task_action_base(const dispatcher_start_data_type &start_param)
    : user_id_(0),
      zone_id_(0),
      private_data_(nullptr),
      result_(0),
      response_code_(0),
      response_message_disabled_(false),
      event_disabled_(false),
      dispatcher_options_(start_param.options),
      shared_context_(rpc::context::create_without_task()) {
  if (nullptr != start_param.context) {
    set_caller_context(*start_param.context);
  }
}

SERVER_FRAME_API task_action_base::~task_action_base() {}

SERVER_FRAME_API const char *task_action_base::name() const {
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
SERVER_FRAME_API task_action_base::result_type task_action_base::operator()(task_action_meta_data_type &&task_meta,
                                                                            dispatcher_start_data_type &&start_data) {
#else
SERVER_FRAME_API int task_action_base::operator()(void *priv_data) {
#endif
  detail::task_action_stat_guard stat(this);

  rpc::context::trace_start_option trace_start_option = get_trace_option();
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  {
#else
  if (nullptr != priv_data) {
    dispatcher_start_data_type &start_data = *reinterpret_cast<dispatcher_start_data_type *>(priv_data);
#endif
    // Set parent context if not set by child type
    if (nullptr != start_data.context) {
      set_caller_context(*start_data.context);
    }
    if (start_data.options != nullptr) {
      dispatcher_options_ = start_data.options;
    }
  }

  task_trace_attributes trace_attributes;
  trace_attributes[static_cast<size_t>(trace_attribute_type::kRpcSystem)] = {
      opentelemetry::trace::SemanticConventions::kRpcSystem,
      trace_start_option.dispatcher ? (rpc::telemetry::semantic_conventions::kRpcSystemValueAtRpcDistapcher)
                                    : (rpc::telemetry::semantic_conventions::kRpcSystemValueAtRpcTask)};
  trace_attributes[static_cast<size_t>(trace_attribute_type::kRpcService)] = {
      opentelemetry::trace::SemanticConventions::kRpcService,
      trace_start_option.dispatcher
          ? rpc::context::string_view{trace_start_option.dispatcher->name()}
          : rpc::context::string_view{rpc::telemetry::semantic_conventions::kRpcServiceValueNoDispatcher}};
  trace_attributes[static_cast<size_t>(trace_attribute_type::kRpcMethod)] = {
      opentelemetry::trace::SemanticConventions::kRpcMethod, rpc::context::string_view{name()}};
  trace_start_option.attributes = trace_attributes;

  trace_start_option.attributes =
      rpc::telemetry::trace_attributes_type{trace_attributes, static_cast<size_t>(trace_attribute_type::kAtRpcKind)};
  rpc::context::tracer tracer = shared_context_.make_tracer(name(), std::move(trace_start_option));

  trace_attributes[static_cast<size_t>(trace_attribute_type::kAtRpcKind)] = {
      rpc::telemetry::semantic_conventions::kAtRpcKind, tracer.get_span_kind()};
  trace_attributes[static_cast<size_t>(trace_attribute_type::kAtRpcSpanName)] = {
      rpc::telemetry::semantic_conventions::kAtRpcSpanName, name()};

  rpc::context::task_context_data rpc_task_context_data;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  private_data_ = task_meta.private_data;
  rpc_task_context_data.task_id = task_meta.task_id;
  if (0 == task_meta.task_id) {
    FWLOGERROR("task convert failed, must in task.");
    co_return tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_INIT,
                             rpc::telemetry::trace_attributes_type{
                                 trace_attributes, static_cast<size_t>(trace_attribute_type::kTaskResponseCode)}});
  }
#else
  task_type_trait::internal_task_type *task = cotask::this_task::get<task_type_trait::internal_task_type>();
  if (nullptr == task) {
    FWLOGERROR("task convert failed, must in task.");
    return tracer.finish({PROJECT_NAMESPACE_ID::err::EN_SYS_INIT,
                          rpc::telemetry::trace_attributes_type{
                              trace_attributes, static_cast<size_t>(trace_attribute_type::kTaskResponseCode)}});
  }
  private_data_ = task_type_trait::get_private_data(*task);
  rpc_task_context_data.task_id = task->get_id();
#endif

  if (nullptr != private_data_) {
    // setup action
    private_data_->action = this;
  }

  rpc_task_context_data.task_name = name();
  shared_context_.set_task_context(rpc_task_context_data);

  if (0 != get_user_id()) {
    FCTXLOGDEBUG(get_shared_context(), "task {} [{}] for player {}:{} start to run\n", name(), get_task_id(),
                 get_zone_id(), get_user_id());
  } else {
    FCTXLOGDEBUG(get_shared_context(), "task {} [{}] start to run\n", name(), get_task_id());
  }

  result_ = RPC_AWAIT_CODE_RESULT(hook_run());

  if (event_disabled_) {
    if (result_ < 0) {
      FCTXLOGERROR(get_shared_context(), "task {} [{}] without evt ret code ({}){} rsp code ({}){}\n", name(),
                   get_task_id(), protobuf_mini_dumper_get_error_msg(result_), result_,
                   protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
    } else {
      FCTXLOGDEBUG(get_shared_context(), "task {} [{}] without evt ret code ({}){}, rsp code ({}){}\n", name(),
                   get_task_id(), protobuf_mini_dumper_get_error_msg(result_), result_,
                   protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
    }

    if (!response_message_disabled_) {
      send_response();
    }

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    co_return _notify_finished(result_, tracer, trace_attributes);
#else
    return _notify_finished(result_, tracer, trace_attributes);
#endif
  }
  // 响应OnSuccess(这时候任务的status还是running)
  TASK_COMPAT_ASSIGN_CURRENT_STATUS(current_task_status);
  if (!task_type_trait::is_exiting(current_task_status) && result_ >= 0) {
    int ret = 0;
    if (response_code_ < 0) {
      ret = on_failed();
      FCTXLOGINFO(get_shared_context(), "task {} [{}] finished success but response errorcode, rsp code: ({}){}\n",
                  name(), get_task_id(), protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
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

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    co_return _notify_finished(ret, tracer, trace_attributes);
#else
    return _notify_finished(ret, tracer, trace_attributes);
#endif
  }

  if (PROJECT_NAMESPACE_ID::err::EN_SUCCESS == result_) {
    if (task_type_trait::is_timeout(current_task_status)) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT;
    } else if (task_type_trait::is_fault(current_task_status)) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED;
    } else if (task_type_trait::is_cancel(current_task_status)) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED;
    } else if (task_type_trait::is_exiting(current_task_status)) {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING;
    } else {
      result_ = PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN;
    }
  }

  if (PROJECT_NAMESPACE_ID::EN_SUCCESS == response_code_) {
    if (task_type_trait::is_timeout(current_task_status)) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT;
    } else if (task_type_trait::is_fault(current_task_status)) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    } else if (task_type_trait::is_cancel(current_task_status)) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    } else if (task_type_trait::is_exiting(current_task_status)) {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM;
    } else {
      response_code_ = PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN;
    }
  }

  if (0 != get_user_id()) {
    FCTXLOGERROR(get_shared_context(), "task {} [{}] for player {}:{} ret code ({}){}, rsp code ({}){}\n", name(),
                 get_task_id(), get_zone_id(), get_user_id(), protobuf_mini_dumper_get_error_msg(result_), result_,
                 protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
  } else {
    FCTXLOGERROR(get_shared_context(), "task {} [{}] ret code ({}){}, rsp code ({}){}\n", name(), get_task_id(),
                 protobuf_mini_dumper_get_error_msg(result_), result_,
                 protobuf_mini_dumper_get_error_msg(response_code_), response_code_);
  }

  // 响应OnTimeout
  if (task_type_trait::is_timeout(current_task_status)) {
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

  if (result_ >= 0) {
    ret = result_;
  }
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  co_return _notify_finished(ret, tracer, trace_attributes);
#else
  return _notify_finished(ret, tracer, trace_attributes);
#endif
}

SERVER_FRAME_API task_action_base::result_type task_action_base::hook_run() { return (*this)(); }

SERVER_FRAME_API int task_action_base::on_success() { return get_result(); }

SERVER_FRAME_API int task_action_base::on_failed() { return get_result(); }

SERVER_FRAME_API int task_action_base::on_timeout() { return 0; }

SERVER_FRAME_API int task_action_base::on_complete() { return 0; }

SERVER_FRAME_API rpc::context::inherit_options task_action_base::get_inherit_option() const noexcept {
  return rpc::context::inherit_options{rpc::context::parent_mode::kLink, true, false};
}

SERVER_FRAME_API rpc::context::trace_start_option task_action_base::get_trace_option() const noexcept {
  rpc::context::trace_start_option ret;
  ret.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
  ret.is_remote = true;
  ret.dispatcher = get_dispatcher();
  ret.parent_network_span = nullptr;
  ret.parent_memory_span = rpc::context::trace_start_option::span_ptr_type();

  return ret;
}

SERVER_FRAME_API uint64_t task_action_base::get_task_id() const {
  return get_shared_context().get_task_context().task_id;
}

SERVER_FRAME_API void task_action_base::set_user_key(uint64_t user_id, uint32_t zone_id) {
  user_id_ = user_id;
  zone_id_ = zone_id;
  if (user_id != 0 && zone_id != 0 && shared_context_.get_task_context().reference_object_type_id == 0 &&
      shared_context_.get_task_context().reference_object_zone_id == 0 &&
      shared_context_.get_task_context().reference_object_instance_id == 0) {
    shared_context_.update_task_context_reference_object(PROJECT_NAMESPACE_ID::EN_ROT_PLAYER, zone_id, user_id);
  }
}

SERVER_FRAME_API task_action_base::on_finished_callback_handle_t task_action_base::add_on_finished(
    on_finished_callback_fn_t &&fn) {
  return on_finished_callback_.insert(on_finished_callback_.end(), std::move(fn));
}

SERVER_FRAME_API void task_action_base::remove_on_finished(on_finished_callback_handle_t handle) {
  on_finished_callback_.erase(handle);
}

SERVER_FRAME_API void task_action_base::set_caller_context(rpc::context &ctx) {
  get_shared_context().set_parent_context(ctx, get_inherit_option());
}

task_action_base::result_type::value_type task_action_base::_notify_finished(int32_t final_result,
                                                                             rpc::context::tracer &tracer,
                                                                             task_trace_attributes &attributes) {
  attributes[static_cast<size_t>(trace_attribute_type::kTaskResponseCode)] = {
      rpc::telemetry::semantic_conventions::kAtRpcResponseCode, get_response_code()};

  // Additional trace data
  auto trace_span = shared_context_.get_trace_span();
  if (trace_span) {
    if (0 != get_user_id() && 0 != get_zone_id()) {
      trace_span->SetAttribute("user_id", get_user_id());
      trace_span->SetAttribute("zone_id", get_zone_id());
    }
    trace_span->SetAttribute(rpc::telemetry::semantic_conventions::kAtRpcResponseCode, get_response_code());
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

  return tracer.finish({final_result, attributes});
}
