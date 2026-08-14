// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py

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

#include <config/extern_service_types.h>

#include <rpc/dtmq/dtmq_client_subscriber.h>
#include <rpc/rpc_common_types.h>
#include <rpc/rpc_context.h>

#include <utility>

TEAM_SERVICE_ROOM_API task_action_channel_event_sync::task_action_channel_event_sync(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

TEAM_SERVICE_ROOM_API task_action_channel_event_sync::~task_action_channel_event_sync() {}

TEAM_SERVICE_ROOM_API const char* task_action_channel_event_sync::name() const {
  return "task_action_channel_event_sync";
}

TEAM_SERVICE_ROOM_API task_action_channel_event_sync::result_type task_action_channel_event_sync::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  auto result = RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_receive_channel_event(
      get_shared_context(), get_request_node_id(), req_body));
  if (result < 0) {
    FCTXLOGERROR(get_shared_context(), "global_receive_channel_event failed: {}({})", result,
                 protobuf_mini_dumper_get_error_msg(result));
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

TEAM_SERVICE_ROOM_API int task_action_channel_event_sync::on_success() { return get_result(); }

TEAM_SERVICE_ROOM_API int task_action_channel_event_sync::on_failed() { return get_result(); }
