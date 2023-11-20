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

SERVER_FRAME_API task_action_no_req_base::task_action_no_req_base(const ctor_param_t& param)
    : task_action_base(make_from_context(param)) {}

SERVER_FRAME_API task_action_no_req_base::~task_action_no_req_base() {}

SERVER_FRAME_API void task_action_no_req_base::send_response() {}

SERVER_FRAME_API std::shared_ptr<dispatcher_implement> task_action_no_req_base::get_dispatcher() const {
  return nullptr;
}

SERVER_FRAME_API const char* task_action_no_req_base::get_type_name() const { return "background"; }

SERVER_FRAME_API rpc::context::inherit_options task_action_no_req_base::get_inherit_option() const noexcept {
  return rpc::context::inherit_options{rpc::context::parent_mode::kParent, true, true};
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
