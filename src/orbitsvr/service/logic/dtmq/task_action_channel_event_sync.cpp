// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-07 15:00:19

#include "logic/dtmq/task_action_channel_event_sync.h"

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

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_common_types.h>

#include <config/extern_service_types.h>

#include <utility>

ORBIT_SERVER_API task_action_channel_event_sync::task_action_channel_event_sync(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_SERVER_API task_action_channel_event_sync::~task_action_channel_event_sync() {}

ORBIT_SERVER_API const char* task_action_channel_event_sync::name() const { return "task_action_channel_event_sync"; }

ORBIT_SERVER_API task_action_channel_event_sync::result_type task_action_channel_event_sync::operator()() {
  const rpc_request_type& req_body = get_request_body();
  disable_response_message();

  auto result = RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
      get_shared_context(), get_request_node_id(), req_body));
  if (result < 0) {
    FWLOGERROR("global_receive_channel_event failed: {}({})", result,
                 protobuf_mini_dumper_get_error_msg(result));
  }
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_SERVER_API int task_action_channel_event_sync::on_success() { return get_result(); }

ORBIT_SERVER_API int task_action_channel_event_sync::on_failed() { return get_result(); }
