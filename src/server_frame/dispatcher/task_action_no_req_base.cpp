// Copyright 2021 atframework
// Created by owent on 2016/9/26.
//

#include "dispatcher/task_action_no_req_base.h"

#include <rpc/db/uuid.h>
#include <rpc/rpc_utils.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <list>
#include <memory>
#include <mutex>

namespace {
static std::recursive_mutex& get_handle_lock() {
  static std::recursive_mutex ret;
  return ret;
}
}  // namespace

std::list<rpc::result_code_type (*)(rpc::context&, task_action_no_req_base&)> task_action_no_req_base::prepare_handles_;

SERVER_FRAME_API task_action_no_req_base::task_action_no_req_base(const ctor_param_t& param)
    : task_action_base(make_from_context(param)) {}

SERVER_FRAME_API task_action_no_req_base::~task_action_no_req_base() {}

SERVER_FRAME_API void task_action_no_req_base::send_response() {}

SERVER_FRAME_API std::shared_ptr<dispatcher_implement> task_action_no_req_base::get_dispatcher() const {
  return nullptr;
}

SERVER_FRAME_API const char* task_action_no_req_base::get_type_name() const { return "background"; }

SERVER_FRAME_API void task_action_no_req_base::add_prepare_handle(
    rpc::result_code_type (*fn)(rpc::context&, task_action_no_req_base&)) {
  if (fn == nullptr) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock_guard{get_handle_lock()};
  prepare_handles_.push_back(fn);
}

SERVER_FRAME_API task_action_no_req_base::result_type task_action_no_req_base::hook_run() {
  // prepare handle
  {
    std::lock_guard<std::recursive_mutex> lock_guard{get_handle_lock()};
    for (auto& fn : prepare_handles_) {
      auto res = RPC_AWAIT_CODE_RESULT(fn(get_shared_context(), *this));
      if (res < 0) {
        TASK_ACTION_RETURN_CODE(res);
      }
    }
  }

  result_type::value_type ret = RPC_AWAIT_CODE_RESULT(base_type::hook_run());
  TASK_ACTION_RETURN_CODE(ret);
}

SERVER_FRAME_API rpc::context::inherit_options task_action_no_req_base::get_inherit_option() const noexcept {
  return rpc::context::inherit_options{rpc::context::parent_mode::kParent, false, true};
}

SERVER_FRAME_API rpc::context::trace_start_option task_action_no_req_base::get_trace_option() const noexcept {
  rpc::context::trace_start_option ret;
  ret.kind = ::atframework::RpcTraceSpan::SPAN_KIND_INTERNAL;
  ret.is_remote = false;
  ret.dispatcher = nullptr;
  ret.parent_network_span = nullptr;
  ret.parent_memory_span = rpc::context::trace_start_option::span_ptr_type();

  return ret;
}

SERVER_FRAME_API dispatcher_start_data_type
task_action_no_req_base::make_from_context(const ctor_param_t& param) noexcept {
  dispatcher_start_data_type ret = dispatcher_make_default<dispatcher_start_data_type>();
  ret.context = param.caller_context;

  return ret;
}
