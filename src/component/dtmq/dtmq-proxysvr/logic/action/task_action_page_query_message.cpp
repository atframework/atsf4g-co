// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-07-01 17:41:57

#include "task_action_page_query_message.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

DTMQ_PROXY_SERVICE_API task_action_page_query_message::task_action_page_query_message(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

DTMQ_PROXY_SERVICE_API task_action_page_query_message::~task_action_page_query_message() {}

DTMQ_PROXY_SERVICE_API const char* task_action_page_query_message::name() const {
  return "task_action_page_query_message";
}

DTMQ_PROXY_SERVICE_API task_action_page_query_message::result_type task_action_page_query_message::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

DTMQ_PROXY_SERVICE_API int task_action_page_query_message::on_success() { return get_result(); }

DTMQ_PROXY_SERVICE_API int task_action_page_query_message::on_failed() { return get_result(); }
