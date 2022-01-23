// Copyright 2021 atframework
// Created by owent on 2016/11/14.
//

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/extern_log_categorize.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <rpc/db/uuid.h>
#include <utility/protobuf_mini_dumper.h>

#include <utility>

#include "dispatcher/actor_action_base.h"

namespace detail {
struct actor_action_stat_guard {
  explicit actor_action_stat_guard(actor_action_base *act) : action(act) {
    if (nullptr != action) {
      util::time::time_utility::update();
      start = util::time::time_utility::now();
    }
  }

  ~actor_action_stat_guard() {
    if (nullptr == action) {
      return;
    }

    util::time::time_utility::update();
    util::time::time_utility::raw_time_t end = util::time::time_utility::now();
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

  util::time::time_utility::raw_time_t start;
  actor_action_base *action;
};
}  // namespace detail

actor_action_base::actor_action_base()
    : user_id_(0),
      zone_id_(0),
      result_(0),
      response_code_(0),
      status_(EN_AAS_CREATED),
      response_message_disabled_(false),
      event_disabled_(false),
      start_data_(dispatcher_make_default<dispatcher_start_data_t>()) {}

actor_action_base::actor_action_base(rpc::context *caller_context)
    : user_id_(0),
      zone_id_(0),
      result_(0),
      response_code_(0),
      status_(EN_AAS_CREATED),
      response_message_disabled_(false),
      event_disabled_(false),
      start_data_(dispatcher_make_default<dispatcher_start_data_t>()) {
  if (nullptr != caller_context) {
    set_caller_context(*caller_context);
  }
}

actor_action_base::~actor_action_base() {
  if (EN_AAS_FINISHED != status_) {
    FWLOGERROR("actor {} [{}] is created but not run", name(), reinterpret_cast<const void *>(this));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TIMEOUT);
    set_result(PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT);
  }
}

const char *actor_action_base::name() const {
  const char *ret = typeid(*this).name();
  if (nullptr == ret) {
    return "RTTI Unavailable: actor_action_base";
  }

  // some compiler will generate number to mark the type
  while (ret && *ret >= '0' && *ret <= '9') {
    ++ret;
  }
  return ret;
}

actor_action_base::result_type actor_action_base::hook_run() { return (*this)(); }

actor_action_base::result_type actor_action_base::run(void *priv_data) {
  detail::actor_action_stat_guard stat(this);

  rpc::context::trace_option trace_option;
  trace_option.kind = ::atframework::RpcTraceSpan::SPAN_KIND_SERVER;
  trace_option.is_remote = true;
  trace_option.dispatcher = get_dispatcher();
  trace_option.parent_trace_span = get_parent_trace_span();
  if (nullptr != priv_data) {
    start_data_ = *reinterpret_cast<dispatcher_start_data_t *>(priv_data);

    // Set parent context if not set by child type
    if (nullptr != start_data_.context) {
      shared_context_.set_parent_context(*start_data_.context);
    }
  }

  if (get_status() > EN_AAS_CREATED) {
    FWLOGERROR("actor {} [{}] already running", name(), reinterpret_cast<const void *>(this));
    return PROJECT_NAMESPACE_ID::err::EN_SYS_BUSY;
  }

  rpc::context::tracer tracer;
  shared_context_.setup_tracer(tracer, name(), std::move(trace_option));

  status_ = EN_AAS_RUNNING;
  if (0 != get_user_id()) {
    FWLOGDEBUG("actor {} [{}] for player {}:{} start to run\n", name(), reinterpret_cast<const void *>(this),
               get_zone_id(), get_user_id());
  } else {
    FWLOGDEBUG("actor {} [{}] start to run\n", name(), reinterpret_cast<const void *>(this));
  }
  result_ = hook_run();

  // 响应OnSuccess(这时候任务的status还是running)
  int32_t ret = 0;
  if (!event_disabled_) {
    if (response_code_ < 0) {
      ret = on_failed();
      FWLOGINFO("actor {} [{}] finished success but response errorcode, rsp code: {}({})\n", name(),
                reinterpret_cast<const void *>(this), response_code_,
                protobuf_mini_dumper_get_error_msg(response_code_));
    } else {
      ret = on_success();
    }

    int complete_res = on_complete();
    if (0 != complete_res) {
      ret = complete_res;
    }
  }

  if (!response_message_disabled_) {
    send_response();
  }
  status_ = EN_AAS_FINISHED;

  _notify_finished();
  return tracer.return_code(ret);
}

int actor_action_base::on_success() { return 0; }

int actor_action_base::on_failed() { return 0; }

int actor_action_base::on_complete() { return 0; }

const atframework::RpcTraceSpan *actor_action_base::get_parent_trace_span() const { return nullptr; }

void actor_action_base::set_caller_context(rpc::context &ctx) { get_shared_context().set_parent_context(ctx); }

void actor_action_base::_notify_finished() {
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
}
